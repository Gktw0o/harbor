#include <gtest/gtest.h>
#include <string>

#include "adapters/file_log.h"
#include "temp_dir.h"

namespace harbor::adapters
{
namespace
{

using test::ReadFileBytes;
using test::TempDir;

// "2026-10-14T15:30:00.123Z [info] " is 32 bytes, so a 50-character message makes an 83-byte line.
std::string Message(char fill)
{
    return std::string(50, fill);
}

TEST(FileLog, WritesLinesToTheNamedFile)
{
    TempDir dir;
    FileLog log(dir.Path(), L"dock");
    log.Info("first");
    log.Warn("second");

    const auto contents = ReadFileBytes(dir.Path() / L"dock.log");
    EXPECT_NE(contents.find("Z [info] first\n"), std::string::npos) << contents;
    EXPECT_NE(contents.find("Z [warn] second\n"), std::string::npos) << contents;
    EXPECT_LT(contents.find("first"), contents.find("second"));
}

TEST(FileLog, AppendsToAnExistingFile)
{
    TempDir dir;
    FileLog(dir.Path(), L"dock").Info("from the first run");
    FileLog(dir.Path(), L"dock").Info("from the second run");

    const auto contents = ReadFileBytes(dir.Path() / L"dock.log");
    EXPECT_NE(contents.find("from the first run"), std::string::npos) << contents;
    EXPECT_NE(contents.find("from the second run"), std::string::npos) << contents;
}

TEST(FileLog, RollsOverToTheOldFileWhenFull)
{
    TempDir dir;
    FileLog log(dir.Path(), L"dock", 200);
    // Two 83-byte lines fit in 200 bytes; the third rolls over, and so does the fifth.
    for (const char fill : {'a', 'b', 'c', 'd', 'e'})
    {
        log.Info(Message(fill));
    }

    const auto old = ReadFileBytes(dir.Path() / L"dock.old.log");
    const auto current = ReadFileBytes(dir.Path() / L"dock.log");
    EXPECT_EQ(old.size(), 2 * 83u);
    EXPECT_NE(old.find(Message('c')), std::string::npos) << old;
    EXPECT_NE(old.find(Message('d')), std::string::npos) << old;
    EXPECT_EQ(current.size(), 83u);
    EXPECT_NE(current.find(Message('e')), std::string::npos) << current;
}

TEST(FileLog, CreatesMissingDirectories)
{
    TempDir dir;
    const auto nested = dir.Path() / L"a" / L"b";
    FileLog(nested, L"dock").Info("hello");
    EXPECT_NE(ReadFileBytes(nested / L"dock.log").find("hello"), std::string::npos);
}

TEST(FileLog, WorksInANonAsciiDirectory)
{
    TempDir dir(L" Ünïcødé ğ 日本");
    FileLog(dir.Path(), L"dock").Info("\xC3\xBC");
    EXPECT_NE(ReadFileBytes(dir.Path() / L"dock.log").find("[info] \xC3\xBC\n"), std::string::npos);
}

TEST(FileLog, WithoutADirectoryLogsToTheDebuggerOnly)
{
    FileLog log({}, L"dock");
    log.Error("nowhere to go");
}

} // namespace
} // namespace harbor::adapters
