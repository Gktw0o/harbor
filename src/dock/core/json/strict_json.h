#pragma once

// Internal to harbor_dock_core: includes nlohmann/json, which the library links privately.

#include <nlohmann/json.hpp>
#include <string_view>
#include <variant>

namespace harbor::json
{

enum class ParseError
{
    Malformed,
    DuplicateKey,
};

// Parses one JSON document and rejects duplicate keys in any object. nlohmann keeps the last of duplicate
// keys while System.Text.Json sees all of them, so Harbor's C++ and C# readers reject them outright and can
// never disagree about the same bytes. Never throws.
std::variant<nlohmann::json, ParseError> ParseStrict(std::string_view text) noexcept;

} // namespace harbor::json
