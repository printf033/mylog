#include "test_common.hpp"

namespace mylog_test
{
    void testLuaConfig()
    {
        section("lua config");
        const std::filesystem::path good = "config_good.lua";
        const mylog::Logger::Config savedCfg = mylog::Logger::getInstance().getConfig();
        writeTestFile(good, "logger = {\n"
                            "  suppressLevel = \"warn\",\n"
                            "  stdoutVivid = true,\n"
                            "  escapeControlChars = false,\n"
                            "  timestampUtc = true,\n"
                            "  maxLogLength = 4096,\n"
                            "  fatalAction = \"none\"\n"
                            "}\n"
                            "fileManager = {\n"
                            "  fileCapacity = 123456,\n"
                            "  threadBufferCapacity = 1111,\n"
                            "  intervalFlushFile_ms = 2000,\n"
                            "  maxFiles = 5\n"
                            "}\n");
        CHECK(mylog::reloadConfig(good));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));
        const mylog::Logger::Config lcfg = mylog::Logger::getInstance().getConfig();
        CHECK(lcfg.lv2str == &mylog::LV2STR_VIVID);
        CHECK(!lcfg.escapeControlChars);
        CHECK(lcfg.timestampUtc);
        CHECK_EQ(lcfg.maxLogLength, 4096u);
        CHECK(lcfg.fatalAction == mylog::FATAL_ACTION::NONE);
        const mylog::FileManager::Config fcfg = mylog::FileManager::getInstance().getConfig();
        CHECK_EQ(fcfg.fileCapacity, 123456u);
        CHECK_EQ(fcfg.threadBufferCapacity, 1111u);
        CHECK_EQ(fcfg.intervalFlushFile_ms, 2000u);
        CHECK_EQ(fcfg.maxFiles, 5u);

        const std::filesystem::path clampCfg = "config_clamp.lua";
        writeTestFile(clampCfg, "logger = {\n"
                                "  maxLogLength = 1\n"
                                "}\n"
                                "fileManager = {\n"
                                "  maxFiles = 99999\n"
                                "}\n");
        CHECK(mylog::reloadConfig(clampCfg));
        const mylog::Logger::Config clamped = mylog::Logger::getInstance().getConfig();
        CHECK_EQ(clamped.maxLogLength, mylog::detail::kMinMaxLogLength);
        const mylog::FileManager::Config clampedFm = mylog::FileManager::getInstance().getConfig();
        CHECK_EQ(clampedFm.maxFiles, mylog::detail::kMaxMaxFiles);
        mylog::Logger::getInstance().setConfig(savedCfg);

        const std::filesystem::path bad = "config_bad.lua";
        writeTestFile(bad, "logger = { fatalAction = \"invalid_action\" }\n");
        CHECK(!mylog::reloadConfig(bad));

        const std::filesystem::path intLevel = "config_int_level.lua";
        writeTestFile(intLevel, "logger = { suppressLevel = 0 }\n");
        CHECK(!mylog::reloadConfig(intLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));

        const std::filesystem::path badLevel = "config_bad_level.lua";
        writeTestFile(badLevel, "logger = { suppressLevel = 99 }\n");
        CHECK(!mylog::reloadConfig(badLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));

        const std::filesystem::path strLevel = "config_str_level.lua";
        writeTestFile(strLevel, "logger = { suppressLevel = \"info\" }\n");
        CHECK(mylog::reloadConfig(strLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::INFO));

        const std::filesystem::path badStrLevel = "config_bad_str_level.lua";
        writeTestFile(badStrLevel, "logger = { suppressLevel = \"nonexistent\" }\n");
        CHECK(!mylog::reloadConfig(badStrLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::INFO));

        const std::filesystem::path broken = "config_broken.lua";
        writeTestFile(broken, "logger = { suppressLevel = \n");
        CHECK(!mylog::reloadConfig(broken));

        const std::filesystem::path evil = "config_evil.lua";
        const std::string marker = "/tmp/mylog_pwned_marker";
        std::error_code ec;
        std::filesystem::remove(marker, ec);
        writeTestFile(evil, "os.execute(\"touch " + marker + "\")\n");
        CHECK(!mylog::reloadConfig(evil));
        CHECK(!std::filesystem::exists(marker));

        mylog::Logger::getInstance().setConfig(savedCfg);

        std::filesystem::remove(good, ec);
        std::filesystem::remove(bad, ec);
        std::filesystem::remove(intLevel, ec);
        std::filesystem::remove(badLevel, ec);
        std::filesystem::remove(strLevel, ec);
        std::filesystem::remove(badStrLevel, ec);
        std::filesystem::remove(clampCfg, ec);
        std::filesystem::remove(broken, ec);
        std::filesystem::remove(evil, ec);
    }
} // namespace mylog_test
