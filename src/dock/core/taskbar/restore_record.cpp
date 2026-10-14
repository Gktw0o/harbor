#include "core/taskbar/restore_record.h"

#include "core/json/strict_json.h"

namespace harbor::taskbar
{
namespace
{

constexpr std::uint64_t kVersion = 1;
constexpr std::uint64_t kMaxState = 3;

} // namespace

std::string EncodeRestoreRecord(const RestoreRecord& record)
{
    // ordered_json keeps the documented field order for people reading the file.
    const nlohmann::ordered_json document{
        {"v", kVersion},
        {"stateBefore", record.stateBefore},
        {"createdUtc", record.createdUtc},
    };
    return document.dump();
}

std::optional<RestoreRecord> DecodeRestoreRecord(std::string_view text) noexcept
try
{
    const auto parsed = json::ParseStrict(text);
    const auto* document = std::get_if<nlohmann::json>(&parsed);
    if (document == nullptr || !document->is_object() || document->size() != 3)
    {
        return std::nullopt;
    }
    const auto version = document->find("v");
    const auto state = document->find("stateBefore");
    const auto created = document->find("createdUtc");
    if (version == document->end() || state == document->end() || created == document->end())
    {
        return std::nullopt;
    }
    // Negative integers parse as number_integer and fractions as number_float, so both fail here.
    if (!version->is_number_unsigned() || version->get<std::uint64_t>() != kVersion ||
        !state->is_number_unsigned() || state->get<std::uint64_t>() > kMaxState || !created->is_string() ||
        !IsUtcTimestamp(created->get_ref<const std::string&>()))
    {
        return std::nullopt;
    }
    return RestoreRecord{static_cast<std::uint32_t>(state->get<std::uint64_t>()),
                         created->get<std::string>()};
}
catch (...)
{
    return std::nullopt;
}

bool IsUtcTimestamp(std::string_view text) noexcept
{
    constexpr std::string_view kShape = "dddd-dd-ddTdd:dd:ddZ";
    if (text.size() != kShape.size())
    {
        return false;
    }
    for (size_t i = 0; i < text.size(); ++i)
    {
        const bool ok = kShape[i] == 'd' ? text[i] >= '0' && text[i] <= '9' : text[i] == kShape[i];
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

} // namespace harbor::taskbar
