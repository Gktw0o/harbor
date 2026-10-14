#include <gtest/gtest.h>

#include "core/ipc/pipe_name.h"

namespace harbor::ipc
{
namespace
{

// Same cases as PipeNamesTests in Harbor.Core.Tests: both sides must produce identical names.
TEST(PipeName, ControlChannelIncludesSessionId)
{
    EXPECT_EQ(PipeName(kControlChannel, 3), "harbor.control.3");
}

TEST(PipeName, SessionZero)
{
    EXPECT_EQ(PipeName("stats", 0), "harbor.stats.0");
}

TEST(PipeName, MaxSessionId)
{
    EXPECT_EQ(PipeName(kControlChannel, 4294967295u), "harbor.control.4294967295");
}

} // namespace
} // namespace harbor::ipc
