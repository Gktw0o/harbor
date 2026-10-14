// Contract tests: the JSON Schemas and the C++ codec against the shared fixtures in contracts/fixtures.
// Harbor.Core.Tests runs the same fixtures against the C# codec, which together proves schema, C++ and C#
// agree.
//
// Schemas and fixtures are listed when the tests run (not when tests are discovered), so adding a file
// takes effect without rebuilding.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/ipc/codec.h"
#include "core/ipc/line_framer.h"
#include "core/island/icon_file.h"
#include "core/taskbar/restore_record.h"

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

// State files: contracts/state/<name>.schema.json with fixtures in contracts/state/fixtures/<name>/.
// Each name the dock reads needs a decoder here; a new schema without one fails the test.
std::optional<std::string> DecodeAndReencodeState(std::string_view name, std::string_view text)
{
    if (name == "taskbar-restore")
    {
        const auto record = taskbar::DecodeRestoreRecord(text);
        return record ? std::optional(taskbar::EncodeRestoreRecord(*record)) : std::nullopt;
    }
    ADD_FAILURE() << "no decoder for state file " << name;
    return std::nullopt;
}

// State files only Harbor Shell reads: here they are checked against their schema alone.
bool IsReadByTheDock(std::string_view name)
{
    return name != "settings";
}

TEST(StateContracts, FixturesConformAndRoundTrip)
{
    const auto stateDir = ContractsDir() / "state";
    const auto schemas = ListFiles(stateDir, ".schema.json");
    ASSERT_FALSE(schemas.empty()) << stateDir;

    for (const auto& schemaPath : schemas)
    {
        const auto file = schemaPath.filename().string();
        const auto name = file.substr(0, file.size() - std::string_view(".schema.json").size());
        SCOPED_TRACE(name);
        json_validator schema;
        schema.set_root_schema(json::parse(ReadLine(schemaPath)));
        const auto fixtures = stateDir / "fixtures" / name;

        const auto valid = ListFiles(fixtures / "valid", ".json");
        EXPECT_FALSE(valid.empty());
        for (const auto& fixture : valid)
        {
            SCOPED_TRACE(fixture.filename().string());
            const auto text = ReadLine(fixture);
            EXPECT_TRUE(Conforms(schema, json::parse(text))) << "schema rejects: " << text;
            if (!IsReadByTheDock(name))
            {
                continue;
            }
            const auto reencoded = DecodeAndReencodeState(name, text);
            ASSERT_TRUE(reencoded) << "decoder rejects: " << text;
            EXPECT_EQ(json::parse(*reencoded), json::parse(text)) << *reencoded;
        }

        const auto invalid = ListFiles(fixtures / "invalid", ".json");
        EXPECT_FALSE(invalid.empty());
        for (const auto& fixture : invalid)
        {
            SCOPED_TRACE(fixture.filename().string());
            const auto text = ReadLine(fixture);
            const auto document = json::parse(text, nullptr, /*allow_exceptions=*/false);
            if (!document.is_discarded())
            {
                EXPECT_FALSE(Conforms(schema, document)) << "schema accepts: " << text;
            }
            if (IsReadByTheDock(name))
            {
                EXPECT_FALSE(DecodeAndReencodeState(name, text)) << "decoder accepts: " << text;
            }
        }
    }
}

// Icon files (contracts/README.md): Harbor.Core.Tests checks the Shell writes the valid fixtures byte for
// byte; here the dock's reader takes them and refuses the rest.
std::vector<std::uint8_t> ReadBytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(IconContracts, ValidFixturesParse)
{
    const auto fixtures = ListFiles(ContractsDir() / "icons" / "fixtures" / "valid", ".bgra");
    ASSERT_FALSE(fixtures.empty());
    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.filename().string());
        const auto bytes = ReadBytes(fixture);
        const auto icon = island::ParseIconFile(bytes);
        ASSERT_TRUE(icon);
        EXPECT_EQ(icon->pixels.size(), std::size_t{icon->width} * icon->height * 4);
    }

    const auto square = ReadBytes(ContractsDir() / "icons/fixtures/valid/square-2x2.bgra");
    const auto icon = island::ParseIconFile(square);
    ASSERT_TRUE(icon);
    EXPECT_EQ(icon->width, 2u);
    EXPECT_EQ(icon->height, 2u);
    // The second pixel: half-transparent green, premultiplied.
    EXPECT_EQ(std::vector<std::uint8_t>(icon->pixels.begin() + 4, icon->pixels.begin() + 8),
              (std::vector<std::uint8_t>{0, 128, 0, 128}));
}

TEST(IconContracts, InvalidFixturesAreRejected)
{
    const auto fixtures = ListFiles(ContractsDir() / "icons" / "fixtures" / "invalid", ".bgra");
    ASSERT_FALSE(fixtures.empty());
    for (const auto& fixture : fixtures)
    {
        EXPECT_FALSE(island::ParseIconFile(ReadBytes(fixture))) << fixture.filename().string();
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

TEST(Contracts, EncodedSetTaskbarMatchesFixture)
{
    EXPECT_EQ(json::parse(Encode(SetTaskbar{true})),
              json::parse(ReadLine(ContractsDir() / "fixtures/valid/set-taskbar-hidden.json")));
}

TEST(Contracts, EncodedTaskbarStateMatchesFixture)
{
    EXPECT_EQ(
        json::parse(Encode(TaskbarState{false, TaskbarBlock::PanicHotkey})),
        json::parse(ReadLine(ContractsDir() / "fixtures/valid/taskbar-state-blocked-panic-hotkey.json")));
}

TEST(Contracts, EncodedDockItemsMatchesFixture)
{
    const DockItems message{{
        {DockItemKind::App, "Microsoft.Windows.Explorer", "File Explorer", "0123456789abcdef.bgra"},
        {DockItemKind::Path, R"(C:\Tools\Ünïcødé ğ\app.exe)", "Ünïcødé \"app\" 𝔸", std::nullopt},
    }};
    EXPECT_EQ(json::parse(Encode(message)),
              json::parse(ReadLine(ContractsDir() / "fixtures/valid/dock-items.json")));
}

// The largest dockItems the schema allows (40 items, every character four bytes) must fit one line, and the
// encoder must write it byte for byte: an encoder that escaped non-ASCII text would need far more room.
TEST(Contracts, LargestDockItemsFitsTheLineLimit)
{
    const auto line = ReadLine(ContractsDir() / "fixtures/valid/dock-items-largest.json");
    EXPECT_LE(line.size(), LineFramer::kMaxLineBytes);

    const auto decoded = Decode(line);
    ASSERT_TRUE(std::holds_alternative<Message>(decoded));
    const auto& items = std::get<DockItems>(std::get<Message>(decoded)).items;
    EXPECT_EQ(items.size(), DockItems::kMaxItems);
    EXPECT_EQ(Encode(std::get<Message>(decoded)), line);
}

} // namespace
} // namespace harbor::ipc
