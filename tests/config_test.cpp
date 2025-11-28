#include <gtest/gtest.h>

#include <engine/core/config.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

class ConfigFiles : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        directory_ = std::filesystem::temp_directory_path() / (std::string("wind_config_test_") + info->name());
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }

    [[nodiscard]] std::filesystem::path path() const { return directory_ / "config.toml"; }

    void write(const std::filesystem::path& file, const std::string& text) const {
        std::ofstream out(file, std::ios::binary);
        out << text;
    }

    [[nodiscard]] static std::string read(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::filesystem::path directory_;
};

}

TEST_F(ConfigFiles, MissingFileIsAnEmptyConfig) {
    const auto config = engine::Config::load(directory_ / "absent.toml");
    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->empty());
    EXPECT_EQ(config->get_int("audio.volume", 80), 80);
}

TEST_F(ConfigFiles, RoundTripKeepsEveryType) {
    engine::Config config;
    config.set_int("audio.volume", 60);
    config.set_float("audio.pan", -0.25);
    config.set_bool("video.fullscreen", true);
    config.set_string("player.name", "Ada \"the\" Dev\n");
    config.set_int("seed", 9007199254740993LL);
    config.set_float("scale", 2.0);
    ASSERT_TRUE(config.save(path()).has_value());

    const auto loaded = engine::Config::load(path());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), 6u);
    EXPECT_EQ(loaded->get_int("audio.volume", 0), 60);
    EXPECT_DOUBLE_EQ(loaded->get_float("audio.pan", 0.0), -0.25);
    EXPECT_TRUE(loaded->get_bool("video.fullscreen", false));
    EXPECT_EQ(loaded->get_string("player.name", ""), "Ada \"the\" Dev\n");
    EXPECT_EQ(loaded->get_int("seed", 0), 9007199254740993LL);
    // A float that is a whole number stays a float in the file.
    EXPECT_DOUBLE_EQ(loaded->get_float("scale", 0.0), 2.0);
    EXPECT_EQ(loaded->get_int("scale", -1), -1);
}

TEST_F(ConfigFiles, SavedFileIsPlainTomlWithSections) {
    engine::Config config;
    config.set_int("audio.volume", 60);
    config.set_bool("debug", false);
    ASSERT_TRUE(config.save(path()).has_value());

    const std::string text = read(path());
    EXPECT_NE(text.find("debug = false"), std::string::npos);
    EXPECT_NE(text.find("[audio]"), std::string::npos);
    EXPECT_NE(text.find("volume = 60"), std::string::npos);
}

TEST_F(ConfigFiles, ReadsAHandWrittenFile) {
    write(path(), "name = \"x\"\nlevel = 3\n\n[audio]\nvolume = 55\nmuted = false\nbalance = 0.5\n");
    const auto config = engine::Config::load(path());
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->get_string("name", ""), "x");
    EXPECT_EQ(config->get_int("level", 0), 3);
    EXPECT_EQ(config->get_int("audio.volume", 0), 55);
    EXPECT_FALSE(config->get_bool("audio.muted", true));
    EXPECT_FLOAT_EQ(static_cast<float>(config->get_float("audio.balance", 0.0)), 0.5f);
}

TEST_F(ConfigFiles, GettersFallBackOnMissingKeyAndOnTypeMismatch) {
    engine::Config config;
    config.set_string("name", "x");
    config.set_int("count", 3);
    config.set_float("ratio", 1.5);

    EXPECT_EQ(config.get_int("missing", 7), 7);
    EXPECT_EQ(config.get_int("name", 7), 7);
    EXPECT_EQ(config.get_int("ratio", 7), 7);
    EXPECT_EQ(config.get_string("count", "fallback"), "fallback");
    EXPECT_TRUE(config.get_bool("count", true));
    EXPECT_FALSE(config.get_bool("name", false));
    // An int reads as a float: a hand-written `volume = 1` is not a surprise.
    EXPECT_DOUBLE_EQ(config.get_float("count", 0.0), 3.0);
    EXPECT_DOUBLE_EQ(config.get_float("name", 0.5), 0.5);
}

TEST_F(ConfigFiles, SetReplacesAndRemoveDeletes) {
    engine::Config config;
    config.set_int("audio.volume", 1);
    config.set_string("audio.volume", "loud");
    EXPECT_EQ(config.size(), 1u);
    EXPECT_EQ(config.get_string("audio.volume", ""), "loud");
    EXPECT_TRUE(config.has("audio.volume"));
    EXPECT_TRUE(config.remove("audio.volume"));
    EXPECT_FALSE(config.remove("audio.volume"));
    EXPECT_FALSE(config.has("audio.volume"));
}

TEST_F(ConfigFiles, MalformedFileIsAnErrorAndStaysUntouched) {
    const std::string broken = "volume = = 3\n[audio\n";
    write(path(), broken);
    const auto config = engine::Config::load(path());
    ASSERT_FALSE(config.has_value());
    EXPECT_EQ(config.error(), engine::ConfigError::InvalidToml);
    EXPECT_EQ(read(path()), broken);
}

TEST_F(ConfigFiles, StructureTheConfigCannotHoldIsAnError) {
    for (const char* text : {"list = [1, 2]\n", "[a.b]\nc = 1\n", "when = 1979-05-27\n", "[a]\nl = [true]\n"}) {
        write(path(), text);
        const auto config = engine::Config::load(path());
        ASSERT_FALSE(config.has_value()) << text;
        EXPECT_EQ(config.error(), engine::ConfigError::Unsupported) << text;
    }
}

TEST_F(ConfigFiles, DirectoryInPlaceOfTheFileIsAnIoError) {
    const auto config = engine::Config::load(directory_);
    ASSERT_FALSE(config.has_value());
    EXPECT_EQ(config.error(), engine::ConfigError::Io);
}

TEST_F(ConfigFiles, FailedSaveLeavesTheOldFileAndNoTemp) {
    engine::Config old_config;
    old_config.set_int("level", 1);
    ASSERT_TRUE(old_config.save(path()).has_value());
    const std::string before = read(path());

    // A directory squats on the temp name, so the write cannot even start.
    std::filesystem::create_directory(directory_ / "config.toml.tmp");
    engine::Config next;
    next.set_int("level", 2);
    const auto saved = next.save(path());
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error(), engine::ConfigError::Io);
    EXPECT_EQ(read(path()), before);
}

TEST_F(ConfigFiles, FailedRenameLeavesNoTempFile) {
    // The target is a non-empty directory: the temp file is written, the rename fails, the temp file is removed.
    std::filesystem::create_directory(path());
    write(path() / "keep", "x");
    engine::Config config;
    config.set_int("level", 2);
    const auto saved = config.save(path());
    ASSERT_FALSE(saved.has_value());
    EXPECT_FALSE(std::filesystem::exists(directory_ / "config.toml.tmp"));
    EXPECT_TRUE(std::filesystem::exists(path() / "keep"));
}

TEST_F(ConfigFiles, SaveCreatesParentDirectoriesAndOverwrites) {
    const std::filesystem::path nested = directory_ / "a" / "b" / "config.toml";
    engine::Config config;
    config.set_int("level", 1);
    ASSERT_TRUE(config.save(nested).has_value());
    config.set_int("level", 2);
    ASSERT_TRUE(config.save(nested).has_value());
    EXPECT_FALSE(std::filesystem::exists(nested.string() + ".tmp"));
    EXPECT_EQ(engine::Config::load(nested)->get_int("level", 0), 2);
}

TEST(Config, KeyRules) {
    EXPECT_TRUE(engine::Config::valid_key("volume"));
    EXPECT_TRUE(engine::Config::valid_key("audio.volume"));
    EXPECT_TRUE(engine::Config::valid_key("a-b_c.D9"));
    EXPECT_FALSE(engine::Config::valid_key(""));
    EXPECT_FALSE(engine::Config::valid_key("."));
    EXPECT_FALSE(engine::Config::valid_key(".x"));
    EXPECT_FALSE(engine::Config::valid_key("x."));
    EXPECT_FALSE(engine::Config::valid_key("a.b.c"));
    EXPECT_FALSE(engine::Config::valid_key("has space"));
    EXPECT_FALSE(engine::Config::valid_key("a=b"));
}

TEST(Config, UserConfigPathRejectsABadName) {
    const auto path = engine::user_config_path("bad/org", "game");
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error(), std::make_error_code(std::errc::invalid_argument));
}
