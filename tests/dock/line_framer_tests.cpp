#include <gtest/gtest.h>

#include "core/ipc/line_framer.h"

namespace harbor::ipc
{
namespace
{

using Lines = std::vector<std::string>;

TEST(LineFramer, ReturnsCompleteLine)
{
    LineFramer framer;
    EXPECT_EQ(framer.Push("abc\n"), Lines{"abc"});
}

TEST(LineFramer, HoldsPartialLineUntilTerminated)
{
    LineFramer framer;
    EXPECT_TRUE(framer.Push("ab").empty());
    EXPECT_TRUE(framer.Push("c").empty());
    EXPECT_EQ(framer.Push("d\nef"), Lines{"abcd"});
    EXPECT_EQ(framer.Push("\n"), Lines{"ef"});
}

TEST(LineFramer, SplitsSeveralLinesInOnePush)
{
    LineFramer framer;
    EXPECT_EQ(framer.Push("a\n\nb\n"), (Lines{"a", "", "b"}));
}

TEST(LineFramer, AcceptsLineOfExactlyMaxLength)
{
    LineFramer framer;
    const std::string line(LineFramer::kMaxLineBytes, 'x');
    EXPECT_EQ(framer.Push(line + "\n"), Lines{line});
    EXPECT_EQ(framer.DroppedLineCount(), 0u);
}

TEST(LineFramer, DropsOversizedLineAndResynchronises)
{
    LineFramer framer;
    const std::string tooLong(LineFramer::kMaxLineBytes + 1, 'x');
    EXPECT_EQ(framer.Push(tooLong + "\nnext\n"), Lines{"next"});
    EXPECT_EQ(framer.DroppedLineCount(), 1u);
}

TEST(LineFramer, DropsOversizedLineSpreadOverManyPushes)
{
    LineFramer framer;
    const std::string chunk(1024, 'x');
    for (std::size_t i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(framer.Push(chunk).empty());
    }
    EXPECT_EQ(framer.Push("tail\nok\n"), Lines{"ok"});
    EXPECT_EQ(framer.DroppedLineCount(), 1u);
}

} // namespace
} // namespace harbor::ipc
