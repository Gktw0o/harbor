#include "core/ipc/codec.h"

#include <algorithm>
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

constexpr std::pair<DockItemKind, std::string_view> kDockItemKindNames[] = {
    {DockItemKind::App, "app"},
    {DockItemKind::Path, "path"},
};

std::string_view WireName(DockItemKind kind) noexcept
{
    for (const auto& [value, name] : kDockItemKindNames)
    {
        if (value == kind)
        {
            return name;
        }
    }
    return "app";
}

std::optional<DockItemKind> DockItemKindFrom(const json& value)
{
    if (value.is_string())
    {
        for (const auto& [kind, name] : kDockItemKindNames)
        {
            if (name == value.get_ref<const std::string&>())
            {
                return kind;
            }
        }
    }
    return std::nullopt;
}

// Unicode characters in text the JSON parser has already checked to be UTF-8: every byte that doesn't
// continue a sequence (10xxxxxx) starts a character.
std::size_t CountCharacters(std::string_view utf8) noexcept
{
    std::size_t count = 0;
    for (const char c : utf8)
    {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }
    return count;
}

// A string of 1 to maxLength characters with no control characters (U+0000-U+001F). No byte of a multi-byte
// UTF-8 sequence is below 0x80, so checking bytes is enough.
bool IsBoundedText(const json& value, std::size_t maxLength) noexcept
{
    return value.is_string() && IsWireText(value.get_ref<const std::string&>(), maxLength);
}

// "<16 lowercase hex digits>.bgra": a plain file name, so it can never point outside the icons folder.
bool IsIconFileName(std::string_view name) noexcept
{
    constexpr std::string_view kExtension = ".bgra";
    constexpr std::size_t kDigits = 16;
    return name.size() == kDigits + kExtension.size() && name.ends_with(kExtension) &&
           std::all_of(name.begin(), name.begin() + kDigits,
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// null, or bounded text.
bool IsOptionalText(const json& value, std::size_t maxLength) noexcept
{
    return value.is_null() || IsBoundedText(value, maxLength);
}

// An integer from 1 to 2^32 - 1 (ids and window handles).
std::optional<std::uint32_t> PositiveUint32(const json& value) noexcept
{
    if (!value.is_number_unsigned())
    {
        return std::nullopt;
    }
    const auto number = value.get<std::uint64_t>();
    if (number == 0 || number > std::numeric_limits<std::uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(number);
}

std::optional<std::string> OptionalString(const json& value)
{
    return value.is_null() ? std::nullopt : std::optional(value.get<std::string>());
}

ordered_json OptionalJson(const std::optional<std::string>& value)
{
    return value ? ordered_json(*value) : ordered_json(nullptr);
}

ordered_json TargetJson(const AppTarget& target)
{
    return {{"kind", WireName(target.kind)}, {"target", target.target}};
}

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

template <> struct MessageTraits<DockItems>
{
    static constexpr std::string_view kType = "dockItems";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const DockItems& message)
    {
        auto items = ordered_json::array();
        for (const auto& item : message.items)
        {
            items.push_back({
                {"kind", WireName(item.kind)},
                {"target", item.target},
                {"name", item.name},
                {"icon", item.icon ? ordered_json(*item.icon) : ordered_json(nullptr)},
            });
        }
        return {{"items", std::move(items)}};
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"items"}))
        {
            return *error;
        }
        const auto& items = payload.at("items");
        if (!items.is_array() || items.size() > DockItems::kMaxItems)
        {
            return DecodeError::InvalidField;
        }
        DockItems message;
        message.items.reserve(items.size());
        for (const auto& item : items)
        {
            if (const auto error = CheckKeys(item, {"kind", "target", "name", "icon"}))
            {
                return *error;
            }
            const auto kind = DockItemKindFrom(item.at("kind"));
            const auto& icon = item.at("icon");
            if (!kind || !IsBoundedText(item.at("target"), DockItems::kMaxTargetLength) ||
                !IsBoundedText(item.at("name"), DockItems::kMaxNameLength) ||
                !(icon.is_null() || (icon.is_string() && IsIconFileName(icon.get_ref<const std::string&>()))))
            {
                return DecodeError::InvalidField;
            }
            message.items.push_back({
                .kind = *kind,
                .target = item.at("target").get<std::string>(),
                .name = item.at("name").get<std::string>(),
                .icon = icon.is_null() ? std::nullopt : std::optional(icon.get<std::string>()),
            });
        }
        return Message{std::move(message)};
    }
};

// {"kind", "target"}, as in dockItems; nullopt if it isn't one.
std::optional<AppTarget> TargetFrom(const json& value)
{
    if (CheckKeys(value, {"kind", "target"}))
    {
        return std::nullopt;
    }
    const auto kind = DockItemKindFrom(value.at("kind"));
    if (!kind || !IsBoundedText(value.at("target"), DockItems::kMaxTargetLength))
    {
        return std::nullopt;
    }
    return AppTarget{*kind, value.at("target").get<std::string>()};
}

template <> struct MessageTraits<AppLookup>
{
    static constexpr std::string_view kType = "appLookup";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const AppLookup& message)
    {
        return {
            {"id", message.id},
            {"aumid", OptionalJson(message.aumid)},
            {"exe", OptionalJson(message.exe)},
            {"window", message.window ? ordered_json(*message.window) : ordered_json(nullptr)},
        };
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"id", "aumid", "exe", "window"}))
        {
            return *error;
        }
        const auto id = PositiveUint32(payload.at("id"));
        const auto& window = payload.at("window");
        const auto windowId = PositiveUint32(window);
        if (!id || !IsOptionalText(payload.at("aumid"), AppLookup::kMaxAumidLength) ||
            !IsOptionalText(payload.at("exe"), AppLookup::kMaxExeLength) || !(window.is_null() || windowId))
        {
            return DecodeError::InvalidField;
        }
        return Message{AppLookup{
            .id = *id,
            .aumid = OptionalString(payload.at("aumid")),
            .exe = OptionalString(payload.at("exe")),
            .window = windowId,
        }};
    }
};

template <> struct MessageTraits<AppInfo>
{
    static constexpr std::string_view kType = "appInfo";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const AppInfo& message)
    {
        return {
            {"id", message.id},
            {"name", message.name},
            {"icon", OptionalJson(message.icon)},
            {"pinned", message.pinned ? TargetJson(*message.pinned) : ordered_json(nullptr)},
            {"launch", message.launch ? TargetJson(*message.launch) : ordered_json(nullptr)},
        };
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"id", "name", "icon", "pinned", "launch"}))
        {
            return *error;
        }
        const auto id = PositiveUint32(payload.at("id"));
        const auto& icon = payload.at("icon");
        const auto& pinned = payload.at("pinned");
        const auto& launch = payload.at("launch");
        const auto pinnedTarget = pinned.is_null() ? std::nullopt : TargetFrom(pinned);
        const auto launchTarget = launch.is_null() ? std::nullopt : TargetFrom(launch);
        if (!id || !IsBoundedText(payload.at("name"), DockItems::kMaxNameLength) ||
            !(icon.is_null() || (icon.is_string() && IsIconFileName(icon.get_ref<const std::string&>()))) ||
            (!pinned.is_null() && !pinnedTarget) || (!launch.is_null() && !launchTarget))
        {
            return DecodeError::InvalidField;
        }
        return Message{AppInfo{
            .id = *id,
            .name = payload.at("name").get<std::string>(),
            .icon = OptionalString(icon),
            .pinned = pinnedTarget,
            .launch = launchTarget,
        }};
    }
};

template <> struct MessageTraits<PinRequest>
{
    static constexpr std::string_view kType = "pinRequest";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const PinRequest& message)
    {
        return {
            {"action", message.action == PinAction::Pin ? "pin" : "unpin"},
            {"kind", WireName(message.item.kind)},
            {"target", message.item.target},
        };
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"action", "kind", "target"}))
        {
            return *error;
        }
        const auto& action = payload.at("action");
        const auto kind = DockItemKindFrom(payload.at("kind"));
        if (!action.is_string() || !kind || !IsBoundedText(payload.at("target"), DockItems::kMaxTargetLength))
        {
            return DecodeError::InvalidField;
        }
        const auto& actionName = action.get_ref<const std::string&>();
        if (actionName != "pin" && actionName != "unpin")
        {
            return DecodeError::InvalidField;
        }
        return Message{PinRequest{
            .action = actionName == "pin" ? PinAction::Pin : PinAction::Unpin,
            .item = {*kind, payload.at("target").get<std::string>()},
        }};
    }
};

ordered_json RectJson(const geometry::Rect& rect)
{
    return {{"left", rect.left}, {"top", rect.top}, {"right", rect.right}, {"bottom", rect.bottom}};
}

// {"left", "top", "right", "bottom"}, each an integer within DockState::kMaxCoordinate; nullopt otherwise.
std::optional<geometry::Rect> RectFrom(const json& value)
{
    if (CheckKeys(value, {"left", "top", "right", "bottom"}))
    {
        return std::nullopt;
    }
    int sides[4] = {};
    int* side = sides;
    for (const char* name : {"left", "top", "right", "bottom"})
    {
        const auto& field = value.at(name);
        if (!field.is_number_integer())
        {
            return std::nullopt;
        }
        const auto number = field.get<std::int64_t>();
        if (number < -DockState::kMaxCoordinate || number > DockState::kMaxCoordinate)
        {
            return std::nullopt;
        }
        *side++ = static_cast<int>(number);
    }
    return geometry::Rect{sides[0], sides[1], sides[2], sides[3]};
}

template <> struct MessageTraits<DockState>
{
    static constexpr std::string_view kType = "dockState";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const DockState& message)
    {
        return {
            {"monitor", RectJson(message.monitor)}, {"work", RectJson(message.work)}, {"dpi", message.dpi},
            {"fullScreen", message.fullScreen},     {"visible", message.visible},
        };
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"monitor", "work", "dpi", "fullScreen", "visible"}))
        {
            return *error;
        }
        const auto monitor = RectFrom(payload.at("monitor"));
        const auto work = RectFrom(payload.at("work"));
        const auto& dpi = payload.at("dpi");
        const auto& fullScreen = payload.at("fullScreen");
        const auto& visible = payload.at("visible");
        if (!monitor || !work || !dpi.is_number_integer() || !fullScreen.is_boolean() ||
            !visible.is_boolean())
        {
            return DecodeError::InvalidField;
        }
        const auto dots = dpi.get<std::int64_t>();
        if (dots < DockState::kMinDpi || dots > DockState::kMaxDpi)
        {
            return DecodeError::InvalidField;
        }
        return Message{DockState{
            .monitor = *monitor,
            .work = *work,
            .dpi = static_cast<unsigned>(dots),
            .fullScreen = fullScreen.get<bool>(),
            .visible = visible.get<bool>(),
        }};
    }
};

template <> struct MessageTraits<IslandState>
{
    static constexpr std::string_view kType = "islandState";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const IslandState& message)
    {
        return {
            {"ready", message.ready},
            {"open", message.open},
            {"hotkey", message.hotkey ? ordered_json(*message.hotkey) : ordered_json(nullptr)},
        };
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"ready", "open", "hotkey"}))
        {
            return *error;
        }
        const auto& ready = payload.at("ready");
        const auto& open = payload.at("open");
        const auto& hotkey = payload.at("hotkey");
        if (!ready.is_boolean() || !open.is_boolean() || !(hotkey.is_null() || hotkey.is_number_integer()))
        {
            return DecodeError::InvalidField;
        }
        std::optional<std::uint32_t> key;
        if (!hotkey.is_null())
        {
            const auto code = hotkey.get<std::int64_t>();
            if (code < IslandState::kMinHotkey || code > IslandState::kMaxHotkey)
            {
                return DecodeError::InvalidField;
            }
            key = static_cast<std::uint32_t>(code);
        }
        return Message{IslandState{.ready = ready.get<bool>(), .open = open.get<bool>(), .hotkey = key}};
    }
};

template <> struct MessageTraits<Launch>
{
    static constexpr std::string_view kType = "launch";
    static constexpr std::uint64_t kVersion = 1;
    static ordered_json Payload(const Launch& message)
    {
        return TargetJson(message.item);
    }
    static DecodeResult FromPayload(const json& payload)
    {
        if (const auto error = CheckKeys(payload, {"kind", "target"}))
        {
            return *error;
        }
        const auto item = TargetFrom(payload);
        if (!item)
        {
            return DecodeError::InvalidField;
        }
        return Message{Launch{*item}};
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
         {&DecodeIf<Ping>, &DecodeIf<Pong>, &DecodeIf<SetTaskbar>, &DecodeIf<TaskbarState>,
          &DecodeIf<DockItems>, &DecodeIf<AppLookup>, &DecodeIf<AppInfo>, &DecodeIf<PinRequest>,
          &DecodeIf<DockState>, &DecodeIf<IslandState>, &DecodeIf<Launch>})
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

bool IsWireText(std::string_view text, std::size_t maxLength) noexcept
{
    const auto length = CountCharacters(text);
    return length >= 1 && length <= maxLength && std::none_of(text.begin(), text.end(), [](char c) {
               return static_cast<unsigned char>(c) < 0x20;
           });
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
