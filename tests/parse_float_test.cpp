#include <gtest/gtest.h>

#include <engine/core/parse_float.h>

#include <string>

TEST(ParseFloat, ReadsSignedDecimalsAndExponents) {
    double value = 0.0;
    EXPECT_TRUE(engine::parse_float("12", value));
    EXPECT_DOUBLE_EQ(value, 12.0);
    EXPECT_TRUE(engine::parse_float("-0.5", value));
    EXPECT_DOUBLE_EQ(value, -0.5);
    EXPECT_TRUE(engine::parse_float("+3.25", value));
    EXPECT_DOUBLE_EQ(value, 3.25);
    EXPECT_TRUE(engine::parse_float("1e3", value));
    EXPECT_DOUBLE_EQ(value, 1000.0);
    EXPECT_TRUE(engine::parse_float("2.5E-2", value));
    EXPECT_DOUBLE_EQ(value, 0.025);
    EXPECT_TRUE(engine::parse_float(".5", value));
    EXPECT_DOUBLE_EQ(value, 0.5);
}

TEST(ParseFloat, RejectsWhatIsNotOneNumber) {
    double value = 7.0;
    EXPECT_FALSE(engine::parse_float("", value));
    EXPECT_FALSE(engine::parse_float("abc", value));
    EXPECT_FALSE(engine::parse_float("1.5x", value));
    EXPECT_FALSE(engine::parse_float(" 1", value));
    EXPECT_FALSE(engine::parse_float("inf", value));
    EXPECT_FALSE(engine::parse_float("+-1", value));
    EXPECT_FALSE(engine::parse_float(std::string(80, '1'), value));
    EXPECT_DOUBLE_EQ(value, 7.0);
}

TEST(ParseFloat, PrefixStopsAtTheFirstCharacterThatIsNotPartOfTheNumber) {
    double value = 0.0;
    EXPECT_EQ(engine::parse_float_prefix("10,20", value), 2u);
    EXPECT_DOUBLE_EQ(value, 10.0);
    EXPECT_EQ(engine::parse_float_prefix("-1.5e2]", value), 6u);
    EXPECT_DOUBLE_EQ(value, -150.0);
    EXPECT_EQ(engine::parse_float_prefix("x1", value), 0u);
}
