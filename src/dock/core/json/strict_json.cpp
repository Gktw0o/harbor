#include "core/json/strict_json.h"

#include <set>
#include <string>
#include <vector>

namespace harbor::json
{

std::variant<nlohmann::json, ParseError> ParseStrict(std::string_view text) noexcept
try
{
    std::vector<std::set<std::string, std::less<>>> keysPerObject;
    bool duplicateKey = false;
    const nlohmann::json::parser_callback_t trackKeys = [&](int, nlohmann::json::parse_event_t event,
                                                            nlohmann::json& parsed) {
        switch (event)
        {
        case nlohmann::json::parse_event_t::object_start:
            keysPerObject.emplace_back();
            break;
        case nlohmann::json::parse_event_t::object_end:
            keysPerObject.pop_back();
            break;
        case nlohmann::json::parse_event_t::key:
            duplicateKey |= !keysPerObject.back().insert(parsed.get<std::string>()).second;
            break;
        default:
            break;
        }
        return true;
    };

    auto document = nlohmann::json::parse(text, trackKeys, /*allow_exceptions=*/false);
    if (document.is_discarded())
    {
        return ParseError::Malformed;
    }
    if (duplicateKey)
    {
        return ParseError::DuplicateKey;
    }
    return document;
}
catch (...)
{
    // Parsing doesn't throw; this covers std::bad_alloc.
    return ParseError::Malformed;
}

} // namespace harbor::json
