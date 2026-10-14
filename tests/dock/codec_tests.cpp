#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/ipc/codec.h"

namespace harbor::ipc
{
namespace
{

DecodeError DecodeErrorOf(std::string_view line)
{
    const auto result = Decode(line);
    const auto* error = std::get_if<DecodeError>(&result);
    EXPECT_NE(error, nullptr) << "expected a decode error for: " << line;
    return error != nullptr ? *error : DecodeError::MalformedJson;
}

TEST(Codec, EncodesPingInDocumentedFieldOrder)
{
    EXPECT_EQ(Encode(Ping{42}), R"({"v":1,"type":"ping","payload":{"seq":42}})");
}

TEST(Codec, EncodesPong)
{
    EXPECT_EQ(Encode(Pong{7}), R"({"v":1,"type":"pong","payload":{"seq":7}})");
}

TEST(Codec, RoundTripsBothMessages)
{
    for (const Message& message : {Message{Ping{0}}, Message{Ping{4294967295u}}, Message{Pong{123}}})
    {
        const auto decoded = Decode(Encode(message));
        ASSERT_TRUE(std::holds_alternative<Message>(decoded));
        EXPECT_EQ(std::get<Message>(decoded), message);
    }
}

TEST(Codec, EncodesTheTaskbarMessages)
{
    EXPECT_EQ(Encode(SetTaskbar{true}), R"({"v":1,"type":"setTaskbar","payload":{"hidden":true}})");
    EXPECT_EQ(Encode(TaskbarState{false, TaskbarBlock::HotkeyUnavailable}),
              R"({"v":1,"type":"taskbarState","payload":{"hidden":false,"blocked":"hotkeyUnavailable"}})");
}

TEST(Codec, RoundTripsTheTaskbarMessages)
{
    const std::vector<Message> messages = {
        SetTaskbar{true},
        SetTaskbar{false},
        TaskbarState{true, TaskbarBlock::None},
        TaskbarState{false, TaskbarBlock::KillOnCloseJob},
    };
    for (const auto& message : messages)
    {
        const auto decoded = Decode(Encode(message));
        ASSERT_TRUE(std::holds_alternative<Message>(decoded));
        EXPECT_EQ(std::get<Message>(decoded), message);
    }
}

// Same list as MessageCodecTests in Harbor.Core.Tests: every value has a wire name that maps back.
TEST(Codec, EveryTaskbarBlockHasAWireName)
{
    for (const auto block :
         {TaskbarBlock::None, TaskbarBlock::HotkeyUnavailable, TaskbarBlock::PanicHotkey,
          TaskbarBlock::ExplorerFighting, TaskbarBlock::TaskbarNotFound, TaskbarBlock::KillOnCloseJob})
    {
        EXPECT_EQ(TaskbarBlockFromWireName(ToWireName(block)), block) << ToWireName(block);
    }
    EXPECT_FALSE(TaskbarBlockFromWireName("PanicHotkey"));
}

TEST(Codec, ReportsSpecificErrorsForTheTaskbarMessages)
{
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"setTaskbar","payload":{}})"), DecodeError::MissingField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"setTaskbar","payload":{"hidden":1}})"),
              DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"setTaskbar","payload":{"hidden":true,"x":1}})"),
              DecodeError::UnexpectedField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":2,"type":"setTaskbar","payload":{"hidden":true}})"),
              DecodeError::UnsupportedVersion);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"taskbarState","payload":{"hidden":false,"blocked":"x"}})"),
              DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"taskbarState","payload":{"hidden":false}})"),
              DecodeError::MissingField);
}

// "𝔸" (U+1D538): one character, four bytes in UTF-8, two UTF-16 code units.
constexpr std::string_view kWide = "\xF0\x9D\x94\xB8";

std::string Repeat(std::string_view text, std::size_t count)
{
    std::string result;
    for (std::size_t i = 0; i < count; ++i)
    {
        result.append(text);
    }
    return result;
}

std::string DockItemsLine(std::string_view kind, std::string_view target, std::string_view name,
                          std::string_view icon = "null")
{
    return std::string(R"({"v":1,"type":"dockItems","payload":{"items":[{"kind":")") + std::string(kind) +
           R"(","target":")" + std::string(target) + R"(","name":")" + std::string(name) + R"(","icon":)" +
           std::string(icon) + "}]}}";
}

TEST(Codec, EncodesDockItemsAsUtf8WithoutEscapes)
{
    const DockItems message{{
        {DockItemKind::App, "Microsoft.Windows.Explorer", "File Explorer", "0123456789abcdef.bgra"},
        {DockItemKind::Path, R"(C:\Tools\ğ.exe)", "\"ğ\" " + std::string(kWide), std::nullopt},
    }};
    EXPECT_EQ(Encode(message),
              R"({"v":1,"type":"dockItems","payload":{"items":[)"
              R"({"kind":"app","target":"Microsoft.Windows.Explorer","name":"File Explorer",)"
              R"("icon":"0123456789abcdef.bgra"},)"
              R"({"kind":"path","target":"C:\\Tools\\ğ.exe","name":"\"ğ\" )" +
                  std::string(kWide) + R"(","icon":null}]}})");
    EXPECT_EQ(Encode(DockItems{}), R"({"v":1,"type":"dockItems","payload":{"items":[]}})");
}

TEST(Codec, RoundTripsDockItems)
{
    const std::vector<Message> messages = {
        DockItems{},
        DockItems{{{DockItemKind::Path, R"(C:\a b\c.lnk)", "c", std::nullopt}}},
        DockItems{{
            {DockItemKind::App, "MSEdge", "Microsoft Edge", "fedcba9876543210.bgra"},
            {DockItemKind::App, Repeat(kWide, DockItems::kMaxTargetLength), Repeat(kWide, 128), std::nullopt},
        }},
    };
    for (const auto& message : messages)
    {
        const auto decoded = Decode(Encode(message));
        ASSERT_TRUE(std::holds_alternative<Message>(decoded));
        EXPECT_EQ(std::get<Message>(decoded), message);
    }
}

// Lengths count characters, not bytes or UTF-16 code units, like JSON Schema's maxLength.
TEST(Codec, BoundsDockItemTextInCharacters)
{
    const auto decodes = [](const std::string& line) {
        return std::holds_alternative<Message>(Decode(line));
    };
    EXPECT_TRUE(decodes(DockItemsLine("app", Repeat("x", 260), "x")));
    EXPECT_FALSE(decodes(DockItemsLine("app", Repeat("x", 261), "x")));
    EXPECT_TRUE(decodes(DockItemsLine("app", Repeat(kWide, 260), Repeat(kWide, 128))));
    EXPECT_FALSE(decodes(DockItemsLine("app", "x", Repeat(kWide, 129))));
    EXPECT_FALSE(decodes(DockItemsLine("app", "", "x")));
    EXPECT_FALSE(decodes(DockItemsLine("app", "x", "")));
    // Only C0 control characters are refused: DEL and C1 controls cost at most two bytes.
    EXPECT_FALSE(decodes(DockItemsLine("app", "x", R"(a\u001fb)")));
    EXPECT_TRUE(decodes(DockItemsLine("app", "x", R"(a\u007f\u0085b)")));
}

TEST(Codec, AcceptsOnlyPlainIconFileNames)
{
    const auto decodes = [](std::string_view icon) {
        return std::holds_alternative<Message>(Decode(DockItemsLine("app", "x", "x", icon)));
    };
    EXPECT_TRUE(decodes(R"("0123456789abcdef.bgra")"));
    EXPECT_TRUE(decodes("null"));
    EXPECT_FALSE(decodes(R"("0123456789abcdeg.bgra")"));
    EXPECT_FALSE(decodes(R"("0123456789abcdef.bgraX")"));
    EXPECT_FALSE(decodes(R"("0123456789abcdef0.bgra")"));
    EXPECT_FALSE(decodes(R"("a\\0123456789abcdef.bgra")"));
    EXPECT_FALSE(decodes(R"("")"));
    EXPECT_FALSE(decodes("false"));
}

TEST(Codec, ReportsSpecificErrorsForDockItems)
{
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"dockItems","payload":{}})"), DecodeError::MissingField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"dockItems","payload":{"items":[{"kind":"app"}]}})"),
              DecodeError::MissingField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"dockItems","payload":{"items":[)"
                            R"({"kind":"app","target":"x","name":"x","icon":null,"x":1}]}})"),
              DecodeError::UnexpectedField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"dockItems","payload":{"items":[1]}})"),
              DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(DockItemsLine("App", "x", "x")), DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":2,"type":"dockItems","payload":{"items":[]}})"),
              DecodeError::UnsupportedVersion);

    std::string tooMany = R"({"v":1,"type":"dockItems","payload":{"items":[)";
    for (std::size_t i = 0; i <= DockItems::kMaxItems; ++i)
    {
        tooMany += std::string(i == 0 ? "" : ",") + R"({"kind":"app","target":"x","name":"x","icon":null})";
    }
    tooMany += "]}}";
    EXPECT_EQ(DecodeErrorOf(tooMany), DecodeError::InvalidField);
}

TEST(Codec, AcceptsFieldsInAnyOrderAndWhitespace)
{
    const auto decoded = Decode(R"( { "payload" : { "seq" : 5 }, "type" : "pong", "v" : 1 } )");
    ASSERT_TRUE(std::holds_alternative<Message>(decoded));
    EXPECT_EQ(std::get<Message>(decoded), Message{Pong{5}});
}

TEST(Codec, ReportsSpecificErrors)
{
    EXPECT_EQ(DecodeErrorOf(""), DecodeError::MalformedJson);
    EXPECT_EQ(DecodeErrorOf("{"), DecodeError::MalformedJson);
    EXPECT_EQ(DecodeErrorOf("\"ping\""), DecodeError::NotAnObject);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping"})"), DecodeError::MissingField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping","payload":{"seq":1},"x":0})"),
              DecodeError::UnexpectedField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping","payload":{"seq":1,"x":0}})"),
              DecodeError::UnexpectedField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":-1,"type":"ping","payload":{"seq":1}})"), DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping","payload":{"seq":-1}})"), DecodeError::InvalidField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"nope","payload":{"seq":1}})"), DecodeError::UnknownType);
    EXPECT_EQ(DecodeErrorOf(R"({"v":2,"type":"pong","payload":{"seq":1}})"), DecodeError::UnsupportedVersion);
}

// JSON Schema can't express "no duplicate keys", so this lives in unit tests on both sides
// (see MessageCodecTests.RejectsDuplicateKeys in Harbor.Core.Tests) instead of contract fixtures.
TEST(Codec, RejectsDuplicateKeys)
{
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"v":1,"type":"ping","payload":{"seq":1}})"),
              DecodeError::UnexpectedField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping","payload":{"seq":1,"seq":2}})"),
              DecodeError::UnexpectedField);
    // Keys are tracked per object: "v" inside payload is not a duplicate of the top-level "v", so the
    // error is the missing seq, not a duplicate.
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"ping","payload":{"v":1}})"), DecodeError::MissingField);
    EXPECT_EQ(DecodeErrorOf(R"({"v":1,"type":"dockItems","payload":{"items":[)"
                            R"({"kind":"app","target":"x","name":"x","name":"y","icon":null}]}})"),
              DecodeError::UnexpectedField);
}

TEST(Codec, RejectsInvalidUtf8)
{
    EXPECT_EQ(DecodeErrorOf("{\"v\":1,\"type\":\"p\xff\",\"payload\":{\"seq\":1}}"),
              DecodeError::MalformedJson);
}

TEST(Codec, RejectsDeeplyNestedInputWithoutCrashing)
{
    const std::string nested = std::string(30000, '[') + std::string(30000, ']');
    EXPECT_EQ(DecodeErrorOf(nested), DecodeError::NotAnObject);
}

} // namespace
} // namespace harbor::ipc
