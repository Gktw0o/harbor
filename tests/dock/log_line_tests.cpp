#include <gtest/gtest.h>

#include "core/log/log_line.h"

namespace harbor::log
{
namespace
{

// Same cases as LogLineTests in Harbor.Core.Tests: the dock and Shell logs share one format.
TEST(LogLine, FormatsTimestampLevelAndMessage)
{
    EXPECT_EQ(FormatLine({2026, 10, 14, 15, 30, 0, 123}, Level::Info, "dock started"),
              "2026-10-14T15:30:00.123Z [info] dock started\n");
}

TEST(LogLine, PadsEveryField)
{
    EXPECT_EQ(FormatLine({2026, 1, 2, 3, 4, 5, 6}, Level::Warning, "x"),
              "2026-01-02T03:04:05.006Z [warn] x\n");
}

TEST(LogLine, EscapesLineBreaksSoEachEntryIsOneLine)
{
    EXPECT_EQ(FormatLine({2026, 10, 14, 15, 30, 0, 0}, Level::Error, "a\r\nb\nc"),
              "2026-10-14T15:30:00.000Z [error] a\\r\\nb\\nc\n");
}

TEST(LogLine, KeepsUtf8Text)
{
    EXPECT_EQ(FormatLine({2026, 10, 14, 15, 30, 0, 0}, Level::Info, "\xC3\x9Cn\xC3\xAF \xC4\x9F"),
              "2026-10-14T15:30:00.000Z [info] \xC3\x9Cn\xC3\xAF \xC4\x9F\n");
}

} // namespace
} // namespace harbor::log
