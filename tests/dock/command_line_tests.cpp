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

TEST(CommandLine, AcceptsPipeNameAndDataDir)
{
    const auto options = std::get<DockOptions>(
        Parse({L"--pipe-name", L"harbor.test.1", L"--data-dir", L"C:\\temp\\x\\..\\h"}));
    EXPECT_EQ(options.pipeName, "harbor.test.1");
    EXPECT_EQ(options.dataDir, std::filesystem::path(L"C:\\temp\\h"));
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
