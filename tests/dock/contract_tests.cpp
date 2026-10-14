// Contract tests: the JSON Schemas and the C++ codec against the shared fixtures in contracts/fixtures.
// Harbor.Core.Tests runs the same fixtures against the C# codec, which together proves schema, C++ and C#
// agree.
//
// Schemas and fixtures are listed when the tests run (not when tests are discovered), so adding a file
// takes effect without rebuilding.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/ipc/codec.h"

namespace harbor::ipc
{
namespace
{

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::json_schema::json_validator;

fs::path ContractsDir()
{
    return fs::path(HARBOR_CONTRACTS_DIR);
}

std::vector<fs::path> ListFiles(const fs::path& dir, std::string_view suffix)
{
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(dir, error))
    {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(suffix))
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<fs::path> ListFixtures(const char* kind)
{
    return ListFiles(ContractsDir() / "fixtures" / kind, ".json");
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

// Message type ("ping") -> validator for contracts/<type>.schema.json.
using SchemaSet = std::map<std::string, std::unique_ptr<json_validator>, std::less<>>;

SchemaSet LoadSchemas()
{
    SchemaSet schemas;
    for (const auto& path : ListFiles(ContractsDir(), ".schema.json"))
    {
        auto validator = std::make_unique<json_validator>();
        validator->set_root_schema(json::parse(ReadLine(path)));
        const auto name = path.filename().string();
        schemas.emplace(name.substr(0, name.size() - std::string_view(".schema.json").size()),
                        std::move(validator));
    }
    return schemas;
}

bool Conforms(const json_validator& schema, const json& document)
{
    nlohmann::json_schema::basic_error_handler errors;
    schema.validate(document, errors);
    return !errors;
}

TEST(Contracts, ValidFixturesConformToTheirSchemaAndRoundTrip)
{
    const auto schemas = LoadSchemas();
    ASSERT_FALSE(schemas.empty()) << ContractsDir();
    const auto fixtures = ListFixtures("valid");
    ASSERT_FALSE(fixtures.empty()) << ContractsDir();

    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.filename().string());
        const auto line = ReadLine(fixture);
        const auto document = json::parse(line);

        const auto schema = schemas.find(document.value("type", ""));
        ASSERT_NE(schema, schemas.end()) << "no schema for type of: " << line;
        EXPECT_TRUE(Conforms(*schema->second, document))
            << "schema " << schema->first << " rejects: " << line;

        const auto decoded = Decode(line);
        if (const auto* error = std::get_if<DecodeError>(&decoded))
        {
            ADD_FAILURE() << "decoder rejected (" << ToString(*error) << "): " << line;
            continue;
        }
        const auto reencoded = Encode(std::get<Message>(decoded));
        EXPECT_EQ(json::parse(reencoded), document) << reencoded;
        EXPECT_EQ(reencoded.find('\n'), std::string::npos) << "encoded message would break line framing";
    }
}

TEST(Contracts, InvalidFixturesMatchNoSchemaAndAreRejected)
{
    const auto schemas = LoadSchemas();
    ASSERT_FALSE(schemas.empty()) << ContractsDir();
    const auto fixtures = ListFixtures("invalid");
    ASSERT_FALSE(fixtures.empty()) << ContractsDir();

    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.filename().string());
        const auto line = ReadLine(fixture);

        const auto document = json::parse(line, nullptr, /*allow_exceptions=*/false);
        if (!document.is_discarded())
        {
            for (const auto& [type, schema] : schemas)
            {
                EXPECT_FALSE(Conforms(*schema, document)) << "schema " << type << " accepts: " << line;
            }
        }
        EXPECT_TRUE(std::holds_alternative<DecodeError>(Decode(line))) << "decoder accepts: " << line;
    }
}

TEST(Contracts, EncodedPingMatchesFixture)
{
    EXPECT_EQ(json::parse(Encode(Ping{42})),
              json::parse(ReadLine(ContractsDir() / "fixtures/valid/ping.json")));
}

TEST(Contracts, EncodedPongMatchesFixture)
{
    EXPECT_EQ(json::parse(Encode(Pong{42})),
              json::parse(ReadLine(ContractsDir() / "fixtures/valid/pong.json")));
}

} // namespace
} // namespace harbor::ipc
