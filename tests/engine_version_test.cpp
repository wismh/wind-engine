#include <gtest/gtest.h>

#include <engine/project/engine_version.h>

#include <string>
#include <string_view>

namespace {

engine::EngineVersion parsed(std::string_view text) {
    const auto version = engine::parse_engine_version(text);
    EXPECT_TRUE(version.has_value()) << text;
    return version.value_or(engine::EngineVersion{});
}

}

TEST(EngineVersion, ParsesEveryShape) {
    EXPECT_EQ(parsed("0.1.7a"), (engine::EngineVersion{0, 1, 7, 'a', false}));
    EXPECT_EQ(parsed("0.2.6b"), (engine::EngineVersion{0, 2, 6, 'b', false}));
    EXPECT_EQ(parsed("1.0.0"), (engine::EngineVersion{1, 0, 0, 0, false}));
    EXPECT_EQ(parsed("0.1.0a-dev"), (engine::EngineVersion{0, 1, 0, 'a', true}));
    EXPECT_EQ(parsed("2.3.4-dev"), (engine::EngineVersion{2, 3, 4, 0, true}));
    EXPECT_EQ(parsed("0.1.100a"), (engine::EngineVersion{0, 1, 100, 'a', false})) << "the patch has no limit";
    EXPECT_EQ(parsed("10.20.3000"), (engine::EngineVersion{10, 20, 3000, 0, false}));
}

TEST(EngineVersion, RejectsWhatIsNotAVersion) {
    for (const std::string_view text : {"", "0", "0.1", "0.1.", "0.1.0.0", ".1.0", "a", "v0.1.0a", "0.1.0c", "0.1.0A",
                 "0.1.0ab", "0.1.0-", "0.1.0-DEV", "0.1.0-rc", "0.1.0a-dev-dev", "0.1.0-deva", "0.1.0a ", " 0.1.0a",
                 "-1.0.0", "+1.0.0", "0.-1.0", "0.1.x", "0.1.0\n", "1,0,0", "99999999999.0.0", "0.0.99999999999a"}) {
        EXPECT_FALSE(engine::parse_engine_version(text).has_value()) << '"' << text << '"';
    }
}

TEST(EngineVersion, ToStringRoundTrips) {
    for (const std::string text : {"0.1.7a", "0.2.6b", "1.0.0", "0.1.0a-dev", "2.3.4-dev", "0.1.100a", "0.0.0"}) {
        EXPECT_EQ(engine::to_string(parsed(text)), text);
    }
}

TEST(EngineVersion, PatchGrowsNumericallyWhateverTheStage) {
    EXPECT_LT(parsed("0.1.99a"), parsed("0.1.100a"));
    EXPECT_LT(parsed("0.1.100a"), parsed("0.2.0a"));
    EXPECT_LT(parsed("0.2.5a"), parsed("0.2.6b"));
    EXPECT_LT(parsed("0.2.5b"), parsed("0.2.6a")) << "the number decides before the stage";
    EXPECT_LT(parsed("0.9.0"), parsed("0.10.0a"));
    EXPECT_LT(parsed("0.99.0"), parsed("1.0.0a"));
}

TEST(EngineVersion, TheStageOrdersOneNumber) {
    EXPECT_LT(parsed("0.2.0a"), parsed("0.2.0b"));
    EXPECT_LT(parsed("0.2.0b"), parsed("0.2.0"));
    EXPECT_LT(parsed("0.2.0a"), parsed("0.2.0"));
}

TEST(EngineVersion, ADevBuildIsOlderThanEveryReleaseOfItsNumber) {
    EXPECT_LT(parsed("0.2.0a-dev"), parsed("0.2.0a"));
    EXPECT_LT(parsed("0.2.0b-dev"), parsed("0.2.0a"));
    EXPECT_LT(parsed("0.2.0-dev"), parsed("0.2.0a"));
    EXPECT_LT(parsed("0.2.0-dev"), parsed("0.2.0"));
    EXPECT_GT(parsed("0.2.0a-dev"), parsed("0.1.99"));
    EXPECT_GT(parsed("0.2.1a-dev"), parsed("0.2.0"));
    EXPECT_LT(parsed("0.2.0a-dev"), parsed("0.2.0b-dev"));
}

TEST(EngineVersion, EqualVersionsCompareEqual) {
    EXPECT_EQ(parsed("0.2.0b") <=> parsed("0.2.0b"), std::strong_ordering::equal);
    EXPECT_NE(parsed("0.2.0a"), parsed("0.2.0"));
    EXPECT_NE(parsed("0.2.0a"), parsed("0.2.0a-dev"));
}
