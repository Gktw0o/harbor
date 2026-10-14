#include "core/ipc/codec.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "core/json/strict_json.h"

namespace harbor::ipc
{
namespace
{

using nlohmann::json;
using nlohmann::ordered_json;

constexpr std::pair<TaskbarBlock, std::string_view> kTaskbarBlockNames[] = {
    {TaskbarBlock::None, "none"},
    {TaskbarBlock::HotkeyUnavailable, "hotkeyUnavailable"},
    {TaskbarBlock::PanicHotkey, "panicHotkey"},
    {TaskbarBlock::ExplorerFighting, "explorerFighting"},
    {TaskbarBlock::TaskbarNotFound, "taskbarNotFound"},
    {TaskbarBlock::KillOnCloseJob, "killOnCloseJob"},
};

// The payload must be an object with exactly these keys. Returns nullopt if it is.
template <size_t N> std::optional<DecodeError> CheckKeys(const json& payload, const char* const (&keys)[N])
{
    if (!payload.is_object())
    {
        return DecodeError::InvalidField;
    }
    for (const char* key : keys)
    {
        if (!payload.contains(key))
        {
            return DecodeError::MissingField;
        }
    }
    if (payload.size() != N)
    {
        return DecodeError::UnexpectedField;
    }
    return std::nullopt;
}

// Payload of both ping and pong: exactly {"seq": <uint32>}.
template <typename T> DecodeResult DecodeSeqPayload(const json& payload)
{
    if (const auto error = CheckKeys(payload, {"seq"}))
    {
        return *error;
    }
    // Negative integers parse as number_integer and fractions as number_float, so both fail here.
    const auto& seq = payload.at("seq");
    if (!seq.is_number_unsigned() || seq.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    {
        return DecodeError::InvalidField;
    }
    return Message{T{static_cast<std::uint32_t>(seq.get<std::uint64_t>())}};
}

// Each message type's name, version and payload.
template <typename T> struct MessageTraits;

template <> struct MessageTraits<Ping>
{
    static constexpr std::string_view kType = "ping";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const Ping& message)
    {
        return {{"seq", message.seq}};
    }
    static DecodeResult FromPayload(const json& payload)
    {
        return DecodeSeqPayload<Ping>(payload);
    }
};

template <> struct MessageTraits<Pong>
{
    static constexpr std::string_view kType = "pong";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const Pong& message)
    {
        return {{"seq", message.seq}};
    }
    static DecodeResult FromPayload(const json& payload)
    {
        return DecodeSeqPayload<Pong>(payload);
    }
};

template <> struct MessageTraits<SetTaskbar>
{
    static constexpr std::string_view kType = "setTaskbar";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const SetTaskbar& message)
    {
        return {{"hidden", message.hidden}};
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"hidden"}))
        {
            return *error;
        }
        const auto& hidden = payload.at("hidden");
        if (!hidden.is_boolean())
        {
            return DecodeError::InvalidField;
        }
        return Message{SetTaskbar{hidden.get<bool>()}};
    }
};

template <> struct MessageTraits<TaskbarState>
{
    static constexpr std::string_view kType = "taskbarState";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const TaskbarState& message)
    {
        return {{"hidden", message.hidden}, {"blocked", ToWireName(message.blocked)}};
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"hidden", "blocked"}))
        {
            return *error;
        }
        const auto& hidden = payload.at("hidden");
        const auto& blocked = payload.at("blocked");
        if (!hidden.is_boolean() || !blocked.is_string())
        {
            return DecodeError::InvalidField;
        }
        const auto block = TaskbarBlockFromWireName(blocked.get_ref<const std::string&>());
        if (!block)
        {
            return DecodeError::InvalidField;
        }
        return Message{TaskbarState{hidden.get<bool>(), *block}};
    }
};

template <typename T>
std::optional<DecodeResult> DecodeIf(std::string_view type, std::uint64_t version, const json& payload)
{
    if (type != MessageTraits<T>::kType)
    {
        return std::nullopt;
    }
    if (version != MessageTraits<T>::kVersion)
    {
        return DecodeError::UnsupportedVersion;
    }
    return MessageTraits<T>::FromPayload(payload);
}

DecodeResult DecodeDocument(const json& document)
{
    if (!document.is_object())
    {
        return DecodeError::NotAnObject;
    }
    const auto version = document.find("v");
    const auto type = document.find("type");
    const auto payload = document.find("payload");
    if (version == document.end() || type == document.end() || payload == document.end())
    {
        return DecodeError::MissingField;
    }
    if (document.size() != 3)
    {
        return DecodeError::UnexpectedField;
    }
    if (!version->is_number_unsigned() || !type->is_string())
    {
        return DecodeError::InvalidField;
    }

    const auto& typeName = type->get_ref<const std::string&>();
    const auto versionNumber = version->get<std::uint64_t>();
    for (const auto decode :
         {&DecodeIf<Ping>, &DecodeIf<Pong>, &DecodeIf<SetTaskbar>, &DecodeIf<TaskbarState>})
    {
        if (auto result = decode(typeName, versionNumber, *payload))
        {
            return std::move(*result);
        }
    }
    return DecodeError::UnknownType;
}

} // namespace

std::string Encode(const Message& message)
{
    return std::visit(
        [](const auto& typed) {
            using T = std::decay_t<decltype(typed)>;
            // ordered_json keeps the documented field order ("v", "type", "payload") for readable logs.
            const ordered_json document{
                {"v", MessageTraits<T>::kVersion},
                {"type", MessageTraits<T>::kType},
                {"payload", MessageTraits<T>::Payload(typed)},
            };
            return document.dump();
        },
        message);
}

std::string_view ToWireName(TaskbarBlock block) noexcept
{
    for (const auto& [value, name] : kTaskbarBlockNames)
    {
        if (value == block)
        {
            return name;
        }
    }
    return "none";
}

std::optional<TaskbarBlock> TaskbarBlockFromWireName(std::string_view name) noexcept
{
    for (const auto& [value, wireName] : kTaskbarBlockNames)
    {
        if (wireName == name)
        {
            return value;
        }
    }
    return std::nullopt;
}

DecodeResult Decode(std::string_view line) noexcept
{
    try
    {
        const auto parsed = harbor::json::ParseStrict(line);
        if (const auto* error = std::get_if<harbor::json::ParseError>(&parsed))
        {
            return *error == harbor::json::ParseError::DuplicateKey ? DecodeError::UnexpectedField
                                                                    : DecodeError::MalformedJson;
        }
        return DecodeDocument(std::get<json>(parsed));
    }
    catch (...)
    {
        // Every access is type-checked first; this is the last line of defense for "bad IPC input never
        // crashes the dock" (e.g. std::bad_alloc).
        return DecodeError::MalformedJson;
    }
}

const char* ToString(DecodeError error) noexcept
{
    switch (error)
    {
    case DecodeError::MalformedJson:
        return "malformed JSON";
    case DecodeError::NotAnObject:
        return "not a JSON object";
    case DecodeError::MissingField:
        return "missing field";
    case DecodeError::UnexpectedField:
        return "unexpected field";
    case DecodeError::InvalidField:
        return "invalid field";
    case DecodeError::UnknownType:
        return "unknown message type";
    case DecodeError::UnsupportedVersion:
        return "unsupported message version";
    }
    return "unknown decode error";
}

} // namespace harbor::ipc
