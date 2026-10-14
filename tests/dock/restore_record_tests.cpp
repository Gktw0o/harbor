#include <gtest/gtest.h>

#include "core/taskbar/restore_record.h"

namespace harbor::taskbar
{
namespace
{

// The shared fixtures in contracts/state cover the schema; these cover what JSON Schema can't express.

TEST(RestoreRecord, RoundTrips)
{
    const RestoreRecord record{0, "2026-10-14T15:30:00Z"};
    EXPECT_EQ(EncodeRestoreRecord(record), R"({"v":1,"stateBefore":0,"createdUtc":"2026-10-14T15:30:00Z"})");
    EXPECT_EQ(DecodeRestoreRecord(EncodeRestoreRecord(record)), record);
}

TEST(RestoreRecord, RejectsDuplicateKeys)
{
    EXPECT_FALSE(DecodeRestoreRecord(
        R"({"v":1,"stateBefore":1,"stateBefore":0,"createdUtc":"2026-10-14T15:30:00Z"})"));
}

TEST(RestoreRecord, RejectsEmptyInput)
{
    EXPECT_FALSE(DecodeRestoreRecord(""));
}

TEST(RestoreRecord, TimestampShape)
{
    EXPECT_TRUE(IsUtcTimestamp("2026-10-14T15:30:00Z"));
    EXPECT_FALSE(IsUtcTimestamp("2026-10-14T15:30:00.000Z"));
    EXPECT_FALSE(IsUtcTimestamp("2026-10-14T15:30:00+00:00"));
    EXPECT_FALSE(IsUtcTimestamp("2026-1O-14T15:30:00Z"));
    EXPECT_FALSE(IsUtcTimestamp(""));
}

} // namespace
} // namespace harbor::taskbar
