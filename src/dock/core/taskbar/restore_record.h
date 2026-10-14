#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace harbor::taskbar
{

// Saved just before Harbor turns on the taskbar's auto-hide, so the user's original setting can be put back
// after a crash or a reboot. Stored as %LOCALAPPDATA%\Harbor\taskbar-restore.json; the schema is
// contracts/state/taskbar-restore.schema.json, shared with Harbor.Core's RestoreRecord.
struct RestoreRecord
{
    // SHAppBarMessage(ABM_GETSTATE) before Harbor changed it, 0-3.
    std::uint32_t stateBefore = 0;
    // When the record was written, e.g. "2026-10-14T15:30:00Z".
    std::string createdUtc;

    bool operator==(const RestoreRecord&) const = default;
};

std::string EncodeRestoreRecord(const RestoreRecord& record);

// Strict, like the IPC codec: anything that doesn't match the schema (including duplicate keys) gives
// nullopt. Never throws.
std::optional<RestoreRecord> DecodeRestoreRecord(std::string_view text) noexcept;

// "YYYY-MM-DDTHH:MM:SSZ" with digits in every numeric position.
bool IsUtcTimestamp(std::string_view text) noexcept;

} // namespace harbor::taskbar
