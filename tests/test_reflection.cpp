#include "test_common.hpp"

namespace mylog_test
{
    struct TestUser
    {
        std::string name;
        int age;
        double score;
    };

    enum class TestColor
    {
        Red,
        Green,
        Blue
    };

    void testReflection()
    {
        section("C++26 reflection & formatters");
        useCaptureOutput();

        // 1. 枚举成员数量反射查询
        CHECK_EQ(mylog::reflect::countEnumMembers<mylog::LEVEL>(), 6u);
        CHECK_EQ(mylog::reflect::countEnumMembers<mylog::FATAL_ACTION>(), 3u);
        CHECK_EQ(mylog::reflect::countEnumMembers<TestColor>(), 3u);

        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::NONE), 0u);
        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::EXIT), 1u);
        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::ABORT), 2u);

        // 2. 枚举值转字符串
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::TRACE), std::string_view("TRACE"));
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::INFO), std::string_view("INFO"));
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::FATAL), std::string_view("FATAL"));
        CHECK_EQ(mylog::reflect::enumToString(TestColor::Green), std::string_view("Green"));

        // 3. 枚举反查解析
        auto parsedAct = mylog::reflect::parseEnum<mylog::FATAL_ACTION>("abort");
        CHECK(parsedAct.has_value());
        CHECK(*parsedAct == mylog::FATAL_ACTION::ABORT);

        auto parsedActUpper = mylog::reflect::parseEnum<mylog::FATAL_ACTION>("EXIT");
        CHECK(parsedActUpper.has_value());
        CHECK(*parsedActUpper == mylog::FATAL_ACTION::EXIT);

        auto parsedLevel = mylog::reflect::parseEnum<mylog::LEVEL>("warn");
        CHECK(parsedLevel.has_value());
        CHECK(*parsedLevel == mylog::LEVEL::WARN);

        auto badLevel = mylog::reflect::parseEnum<mylog::LEVEL>("nonexistent");
        CHECK(!badLevel.has_value());

        // 4. 结构体成员数量与反射元信息数组
        CHECK_EQ(mylog::reflect::countMembers<TestUser>(), 3u);
        CHECK_EQ(mylog::reflect::countMembers<mylog::FileManager::Config>(), 5u);
        static_assert(mylog::reflect::getMemberInfos<TestUser>().size() == 3u);

        // 5. 聚合结构体自动格式化输出
        TestUser u{"Alice", 25, 99.5};
        clearCaptured();
        mylog::info("user={}", u);
        CHECK(captured().find("user={name: Alice, age: 25, score: 99.5}") != std::string::npos);

        // 6. 作用域枚举自动格式化输出
        clearCaptured();
        mylog::info("color={}", TestColor::Blue);
        CHECK(captured().find("color=Blue") != std::string::npos);

        clearCaptured();
        mylog::info("level={}", mylog::LEVEL::DEBUG);
        CHECK(captured().find("level=DEBUG") != std::string::npos);

        // 7. 敏感字段注解 [[= mylog::sensitive]] 自动掩码脱敏
        struct TestAccount
        {
            std::string username;
            [[= mylog::sensitive]] std::string password;
            [[= mylog::sensitive]] std::string token;
            int roleId;
        };
        TestAccount acc{"alice", "secret_pass_123", "token_xyz_999", 1};
        clearCaptured();
        mylog::info("account: {}", acc);
        CHECK(captured().find("account: {username: alice, password: \"******\", token: \"******\", roleId: 1}") !=
              std::string::npos);
        CHECK(captured().find("secret_pass_123") == std::string::npos);
        CHECK(captured().find("token_xyz_999") == std::string::npos);

        // 8. 辅助级别/动作转换函数
        CHECK_EQ(mylog::levelToString(mylog::LEVEL::INFO), "INFO");
        CHECK_EQ(mylog::levelToString(mylog::LEVEL::ERROR), "ERROR");
        CHECK(mylog::parseLevel("trace") == mylog::LEVEL::TRACE);
        CHECK(mylog::parseLevel("FATAL") == mylog::LEVEL::FATAL);
        CHECK(!mylog::parseLevel("unknown").has_value());

        CHECK_EQ(mylog::fatalActionToString(mylog::FATAL_ACTION::ABORT), "ABORT");
        CHECK(mylog::parseFatalAction("exit") == mylog::FATAL_ACTION::EXIT);
        CHECK(mylog::parseFatalAction("NONE") == mylog::FATAL_ACTION::NONE);
        CHECK(!mylog::parseFatalAction("unknown").has_value());

        CHECK(mylog::setSuppressLevel("DEBUG"));
        CHECK_EQ(mylog::getCurrentSuppressLevel(), mylog::LEVEL::DEBUG);
        CHECK(!mylog::setSuppressLevel("invalid_level"));
        CHECK_EQ(mylog::getCurrentSuppressLevel(), mylog::LEVEL::DEBUG);
    }

    void testMacroFreeLogging()
    {
        section("C++26 macro-free logging with source_location");
        useCaptureOutput();
        mylog::setSuppressLevel(mylog::LEVEL::TRACE);

        // 1. mylog::info 格式化重载与文件名/行号注入
        clearCaptured();
        const int expectedLine1 = __LINE__ + 1;
        mylog::info("hello {} and {}", "modern", 2026);
        std::string out1 = captured();
        CHECK(out1.find("hello modern and 2026") != std::string::npos);
        CHECK(out1.find("test_reflection.cpp:" + std::to_string(expectedLine1)) != std::string::npos);
        CHECK(out1.find("INFO") != std::string::npos);

        // 2. mylog::debug 纯文本重载
        clearCaptured();
        const int expectedLine2 = __LINE__ + 1;
        mylog::debug("simple debug message");
        std::string out2 = captured();
        CHECK(out2.find("simple debug message") != std::string::npos);
        CHECK(out2.find("test_reflection.cpp:" + std::to_string(expectedLine2)) != std::string::npos);
        CHECK(out2.find("DEBUG") != std::string::npos);

        // 3. mylog::trace / mylog::warn / mylog::error
        clearCaptured();
        mylog::trace("trace log");
        CHECK(captured().find("TRACE") != std::string::npos);

        clearCaptured();
        mylog::warn("warning code: {:04d}", 42);
        CHECK(captured().find("warning code: 0042") != std::string::npos);
        CHECK(captured().find("WARN") != std::string::npos);

        clearCaptured();
        mylog::error("error msg: {}", "failed to connect");
        CHECK(captured().find("error msg: failed to connect") != std::string::npos);
        CHECK(captured().find("ERROR") != std::string::npos);

        // 4. 对象原位打印重载：mylog::info(obj)
        struct SimplePoint
        {
            int x;
            int y;
        };
        clearCaptured();
        mylog::info(SimplePoint{10, 20});
        CHECK(captured().find("{x: 10, y: 20}") != std::string::npos);

        // 5. 无宏 API 级别抑制检查
        clearCaptured();
        mylog::setSuppressLevel(mylog::LEVEL::WARN);
        mylog::info("this should be suppressed");
        mylog::debug("this should also be suppressed");
        CHECK(captured().empty());
        mylog::warn("this should appear");
        CHECK(captured().find("this should appear") != std::string::npos);
        mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    }
} // namespace mylog_test
