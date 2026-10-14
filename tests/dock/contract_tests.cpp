// Contract tests: the C++ codec against the shared fixtures in contracts/fixtures. Harbor.Core.Tests runs
// the same fixtures against the C# codec and the JSON Schemas, which together prove both sides agree.
//
// Fixtures are listed when the tests run (not when tests are discovered), so adding a fixture file takes
// effect without rebuilding.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/ipc/codec.h"

namespace harbor::ipc
{
namespace
{

namespace fs = std::filesystem;

fs::path FixturesDir()
{
    return fs::path(HARBOR_CONTRACTS_DIR) / "fixtures";
}

std::vector<fs::path> ListFixtures(const char* kind)
{
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(FixturesDir() / kind, error))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// One line, as it would arrive from the pipe: trailing line endings are not part of the message.
std::string ReadLine(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    std::string line = contents.str();
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    {
        line.pop_back();
    }
    return line;
}

TEST(Contracts, ValidFixturesDecodeAndReencodeToEqualJson)
{
    const auto fixtures = ListFixtures("valid");
    ASSERT_FALSE(fixtures.empty()) << FixturesDir();

    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.filename().string());
        const auto line = ReadLine(fixture);
        const auto decoded = Decode(line);
        if (const auto* error = std::get_if<DecodeError>(&decoded))
        {
            ADD_FAILURE() << "rejected (" << ToString(*error) << "): " << line;
            continue;
        }

        const auto reencoded = Encode(std::get<Message>(decoded));
        EXPECT_EQ(nlohmann::json::parse(reencoded), nlohmann::json::parse(line)) << reencoded;
        EXPECT_EQ(reencoded.find('\n'), std::string::npos) << "encoded message would break line framing";
    }
}

TEST(Contracts, InvalidFixturesAreRejected)
{
    const auto fixtures = ListFixtures("invalid");
    ASSERT_FALSE(fixtures.empty()) << FixturesDir();

    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.filename().string());
        const auto line = ReadLine(fixture);
        EXPECT_TRUE(std::holds_alternative<DecodeError>(Decode(line))) << "accepted: " << line;
    }
}

TEST(Contracts, EncodedPingMatchesFixture)
{
    EXPECT_EQ(nlohmann::json::parse(Encode(Ping{42})),
              nlohmann::json::parse(ReadLine(FixturesDir() / "valid/ping.json")));
}

TEST(Contracts, EncodedPongMatchesFixture)
{
    EXPECT_EQ(nlohmann::json::parse(Encode(Pong{42})),
              nlohmann::json::parse(ReadLine(FixturesDir() / "valid/pong.json")));
}

} // namespace
} // namespace harbor::ipc
