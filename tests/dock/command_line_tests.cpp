#include <gtest/gtest.h>
#include <initializer_list>
#include <vector>

#include "app/command_line.h"

namespace harbor::dock
{
namespace
{

std::variant<DockOptions, std::string> Parse(std::initializer_list<std::wstring_view> args)
{
    const std::vector<std::wstring_view> list(args);
    return ParseCommandLine(list);
}

bool IsError(const std::variant<DockOptions, std::string>& result)
{
    return std::holds_alternative<std::string>(result);
}

TEST(CommandLine, NoArgumentsMeansDefaults)
{
    const auto options = std::get<DockOptions>(Parse({}));
    EXPECT_FALSE(options.pipeName);
    EXPECT_FALSE(options.dataDir);
}

TEST(CommandLine, AcceptsTheTestOptionsTogether)
{
    const auto options = std::get<DockOptions>(Parse({L"--pipe-name", L"harbor.test.1", L"--data-dir",
                                                      L"C:\\temp\\x\\..\\h", L"--test-taskbar", L"ab-12"}));
    EXPECT_EQ(options.pipeName, "harbor.test.1");
    EXPECT_EQ(options.dataDir, std::filesystem::path(L"C:\\temp\\h"));
    EXPECT_EQ(options.testTaskbarId, L"ab-12");
}

// Otherwise a test could point a dock at the real taskbar, or a test dock could join the real Shell.
TEST(CommandLine, RejectsTestOptionsOnTheirOwn)
{
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"harbor.test.1"})));
    EXPECT_TRUE(IsError(Parse({L"--data-dir", L"C:\\temp"})));
    EXPECT_TRUE(IsError(Parse({L"--test-taskbar", L"ab"})));
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"harbor.test.1", L"--data-dir", L"C:\\temp"})));
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"harbor.test.1", L"--test-taskbar", L"ab"})));
}

TEST(CommandLine, AcceptsATestPingTimeoutInTestMode)
{
    const auto options =
        std::get<DockOptions>(Parse({L"--pipe-name", L"harbor.test.1", L"--data-dir", L"C:\\t",
                                     L"--test-taskbar", L"ab", L"--test-ping-timeout-ms", L"500"}));
    EXPECT_EQ(options.testPingTimeoutMs, 500u);
}

TEST(CommandLine, RejectsATestPingTimeoutOutsideTestModeOrRange)
{
    EXPECT_TRUE(IsError(Parse({L"--test-ping-timeout-ms", L"500"})));
    const auto withTestMode = [](std::wstring_view value) {
        return Parse({L"--pipe-name", L"harbor.test.1", L"--data-dir", L"C:\\t", L"--test-taskbar", L"ab",
                      L"--test-ping-timeout-ms", value});
    };
    EXPECT_TRUE(IsError(withTestMode(L"99")));
    EXPECT_TRUE(IsError(withTestMode(L"60001")));
    EXPECT_TRUE(IsError(withTestMode(L"5s")));
    EXPECT_TRUE(IsError(withTestMode(L"")));
}

TEST(CommandLine, RejectsInvalidTestIds)
{
    EXPECT_TRUE(IsError(Parse({L"--test-taskbar", L"AB"})));
    EXPECT_TRUE(IsError(Parse({L"--test-taskbar", L"a.b"})));
    EXPECT_TRUE(IsError(Parse({L"--test-taskbar", std::wstring(65, L'a')})));
}

TEST(CommandLine, RejectsUnknownArguments)
{
    EXPECT_TRUE(IsError(Parse({L"--verbose"})));
    EXPECT_TRUE(IsError(Parse({L"harbor.test.1"})));
}

TEST(CommandLine, RejectsAMissingValue)
{
    EXPECT_TRUE(IsError(Parse({L"--pipe-name"})));
    EXPECT_TRUE(IsError(Parse({L"--data-dir"})));
    EXPECT_TRUE(IsError(Parse({L"--test-taskbar"})));
}

TEST(CommandLine, RejectsRepeatedOptions)
{
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"harbor.a", L"--pipe-name", L"harbor.b"})));
    EXPECT_TRUE(IsError(Parse({L"--data-dir", L"C:\\a", L"--data-dir", L"C:\\b"})));
}

TEST(CommandLine, RejectsInvalidPipeNames)
{
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"other.control.1"})));
    EXPECT_TRUE(IsError(Parse({L"--pipe-name", L"harbor.\u00FC"})));
}

TEST(CommandLine, RejectsRelativeDataDirs)
{
    EXPECT_TRUE(IsError(Parse({L"--data-dir", L"logs"})));
    EXPECT_TRUE(IsError(Parse({L"--data-dir", L"\\temp"})));
    EXPECT_TRUE(IsError(Parse({L"--data-dir", L"C:temp"})));
}

} // namespace
} // namespace harbor::dock
