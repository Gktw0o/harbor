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

TEST(PipeName, GeneratedNamesAreValid)
{
    EXPECT_TRUE(IsValidPipeName(PipeName(kControlChannel, 4294967295u)));
    EXPECT_TRUE(IsValidPipeName("harbor.e2e.0123456789abcdef0123456789abcdef"));
}

TEST(PipeName, RejectsNamesOutsideHarborNamespace)
{
    EXPECT_FALSE(IsValidPipeName(""));
    EXPECT_FALSE(IsValidPipeName("harbor."));
    EXPECT_FALSE(IsValidPipeName("other.control.1"));
    EXPECT_FALSE(IsValidPipeName("harbor.Control.1"));
    EXPECT_FALSE(IsValidPipeName("harbor.control\\..\\x"));
    EXPECT_FALSE(IsValidPipeName("harbor.control 1"));
    EXPECT_FALSE(IsValidPipeName("harbor." + std::string(201, 'a')));
}

} // namespace
} // namespace harbor::ipc
