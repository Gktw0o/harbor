#include <gtest/gtest.h>

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
