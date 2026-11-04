#include <gtest/gtest.h>

#include <engine/core/server_options.h>

#include <string>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] auto parse(std::vector<std::string> arguments) {
    return parse_server_args(arguments);
}

TEST(ServerArgs, EmptyLineHasNothingSet) {
    const auto options = parse({});
    ASSERT_TRUE(options.has_value());
    EXPECT_FALSE(options->port.has_value());
    EXPECT_TRUE(options->config.empty());
    EXPECT_TRUE(options->assets.empty());
    EXPECT_FALSE(options->help);
    EXPECT_TRUE(options->args.empty());
}

TEST(ServerArgs, PortBothForms) {
    EXPECT_EQ(parse({"--port", "27015"})->port, 27015);
    EXPECT_EQ(parse({"--port=27016"})->port, 27016);
    EXPECT_EQ(parse({"--port", "1"})->port, 1);
    EXPECT_EQ(parse({"--port", "65535"})->port, 65535);
}

TEST(ServerArgs, BadPortNamesTheValue) {
    for (const char* const bad : {"0", "65536", "abc", "27015x", "-1", ""}) {
        const auto options = parse({"--port", bad});
        ASSERT_FALSE(options.has_value()) << bad;
        EXPECT_EQ(options.error().error, ServerArgError::BadPort);
        EXPECT_EQ(options.error().argument, bad);
    }
}

TEST(ServerArgs, MissingValueNamesTheFlag) {
    for (const char* const flag : {"--port", "--config", "--assets"}) {
        const auto options = parse({flag});
        ASSERT_FALSE(options.has_value()) << flag;
        EXPECT_EQ(options.error().error, ServerArgError::MissingValue);
        EXPECT_EQ(options.error().argument, flag);
    }
    EXPECT_FALSE(parse({"--config="}).has_value());
}

TEST(ServerArgs, ConfigAndAssetsArePaths) {
    const auto options = parse({"--config", "a/b.toml", "--assets=c/d"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->config, std::filesystem::path("a/b.toml"));
    EXPECT_EQ(options->assets, std::filesystem::path("c/d"));
}

TEST(ServerArgs, PathsAreUtf8) {
    const auto options = parse({"--config", "\xD0\xB3\xD1\x80\xD0\xB0.toml"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->config, std::filesystem::path(u8"гра.toml"));
}

TEST(ServerArgs, Help) {
    EXPECT_TRUE(parse({"--help"})->help);
    EXPECT_TRUE(parse({"-h"})->help);
    EXPECT_NE(server_usage().find("--port"), std::string_view::npos);
}

TEST(ServerArgs, WhatTheEngineDoesNotTakeGoesToTheGameInOrder) {
    const auto options = parse({"--world", "big", "--port", "5", "--fast"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->port, 5);
    EXPECT_EQ(options->args, (std::vector<std::string>{"--world", "big", "--fast"}));
}

TEST(ServerArgs, DoubleDashEndsTheEnginesFlags) {
    const auto options = parse({"--port", "5", "--", "--port", "6", "x"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->port, 5);
    EXPECT_EQ(options->args, (std::vector<std::string>{"--port", "6", "x"}));
}

}
}
