#include "core/ipc/codec.h"

#include <cstdint>
#include <limits>
#include <type_traits>

#include "core/json/strict_json.h"

namespace harbor::ipc
{
namespace
{

using nlohmann::json;

template <typename T> struct MessageTraits;

template <> struct MessageTraits<Ping>
{
    static constexpr std::string_view kType = "ping";
    static constexpr std::uint64_t kVersion = 1;
};

template <> struct MessageTraits<Pong>
{
    static constexpr std::string_view kType = "pong";
    static constexpr std::uint64_t kVersion = 1;
};

// Payload of both ping and pong: exactly {"seq": <uint32>}.
std::variant<std::uint32_t, DecodeError> DecodeSeqPayload(const json& payload)
{
    if (!payload.is_object())
    {
        return DecodeError::InvalidField;
    }
    const auto seq = payload.find("seq");
    if (seq == payload.end())
    {
        return DecodeError::MissingField;
    }
    if (payload.size() != 1)
    {
        return DecodeError::UnexpectedField;
    }
    // Negative integers parse as number_integer and fractions as number_float, so both fail here.
    if (!seq->is_number_unsigned() || seq->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    {
        return DecodeError::InvalidField;
    }
    return static_cast<std::uint32_t>(seq->get<std::uint64_t>());
}

template <typename T> DecodeResult DecodeSeqMessage(std::uint64_t version, const json& payload)
{
    if (version != MessageTraits<T>::kVersion)
    {
        return DecodeError::UnsupportedVersion;
    }
    const auto seq = DecodeSeqPayload(payload);
    if (const auto* error = std::get_if<DecodeError>(&seq))
    {
        return *error;
    }
    return Message{T{std::get<std::uint32_t>(seq)}};
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
    if (typeName == MessageTraits<Ping>::kType)
    {
        return DecodeSeqMessage<Ping>(versionNumber, *payload);
    }
    if (typeName == MessageTraits<Pong>::kType)
    {
        return DecodeSeqMessage<Pong>(versionNumber, *payload);
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
            const nlohmann::ordered_json document{
                {"v", MessageTraits<T>::kVersion},
                {"type", MessageTraits<T>::kType},
                {"payload", {{"seq", typed.seq}}},
            };
            return document.dump();
        },
        message);
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
