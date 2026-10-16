#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "core/ipc/messages.h"

namespace harbor::ipc
{

enum class DecodeError
{
    MalformedJson,
    NotAnObject,
    MissingField,
    UnexpectedField,
    InvalidField,
    UnknownType,
    UnsupportedVersion,
};

using DecodeResult = std::variant<Message, DecodeError>;

// Encodes a message as one compact JSON line, without the trailing '\n'.
std::string Encode(const Message& message);

// Decodes one line (without its '\n'). Strict: anything that doesn't match contracts/*.schema.json is
// rejected with an error. Never throws.
DecodeResult Decode(std::string_view line) noexcept;

const char* ToString(DecodeError error) noexcept;

// Whether UTF-8 text fits a bounded string field: 1 to maxLength Unicode characters, no control characters.
bool IsWireText(std::string_view text, std::size_t maxLength) noexcept;

// TaskbarBlock as it appears on the wire: "none", "hotkeyUnavailable", ...
std::string_view ToWireName(TaskbarBlock block) noexcept;
std::optional<TaskbarBlock> TaskbarBlockFromWireName(std::string_view name) noexcept;

} // namespace harbor::ipc
