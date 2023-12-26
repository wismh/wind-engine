#include <gtest/gtest.h>

#include "loc/format.h"
#include "loc/plural.h"

#include <string>

namespace {

engine::loc::Arg num(std::string_view name, std::int64_t value) {
    return engine::loc::Arg{name, value};
}

engine::loc::Arg str(std::string_view name, std::string_view value) {
    return engine::loc::Arg{name, value};
}

}

TEST(LocPlural, EnglishAndUkrainianIntegers) {
    EXPECT_EQ(engine::loc::plural_category("en", 0), engine::loc::PluralCategory::Other);
    EXPECT_EQ(engine::loc::plural_category("en", 1), engine::loc::PluralCategory::One);
    EXPECT_EQ(engine::loc::plural_category("en", 2), engine::loc::PluralCategory::Other);
    EXPECT_EQ(engine::loc::plural_category("en", 21), engine::loc::PluralCategory::Other);
    EXPECT_EQ(engine::loc::plural_category("EN-us", 1), engine::loc::PluralCategory::One);

    EXPECT_EQ(engine::loc::plural_category("uk", 1), engine::loc::PluralCategory::One);
    EXPECT_EQ(engine::loc::plural_category("uk", 2), engine::loc::PluralCategory::Few);
    EXPECT_EQ(engine::loc::plural_category("uk", 4), engine::loc::PluralCategory::Few);
    EXPECT_EQ(engine::loc::plural_category("uk", 5), engine::loc::PluralCategory::Many);
    EXPECT_EQ(engine::loc::plural_category("uk", 0), engine::loc::PluralCategory::Many);
    EXPECT_EQ(engine::loc::plural_category("uk", 11), engine::loc::PluralCategory::Many);
    EXPECT_EQ(engine::loc::plural_category("uk", 12), engine::loc::PluralCategory::Many);
    EXPECT_EQ(engine::loc::plural_category("uk", 21), engine::loc::PluralCategory::One);
    EXPECT_EQ(engine::loc::plural_category("uk", 22), engine::loc::PluralCategory::Few);
    EXPECT_EQ(engine::loc::plural_category("uk-UA", 22), engine::loc::PluralCategory::Few);
    EXPECT_EQ(engine::loc::plural_category("ru", 21), engine::loc::PluralCategory::One);
    EXPECT_EQ(engine::loc::plural_category("pl", 2), engine::loc::PluralCategory::Other);
    EXPECT_EQ(engine::loc::plural_category("uk", -1), engine::loc::PluralCategory::One);
}

TEST(LocFormat, SubstitutesNamedArgsInEitherOrder) {
    const engine::loc::Arg args[] = {num("count", 2), str("name", "Ann")};
    const auto formatted = engine::loc::format("{name} has {count}", args, "en");
    ASSERT_TRUE(formatted.has_value());
    EXPECT_EQ(*formatted, "Ann has 2");
}

TEST(LocFormat, EscapesBracesAndLeavesMissingArg) {
    const auto braces = engine::loc::format("{{name}}", {}, "en");
    ASSERT_TRUE(braces.has_value());
    EXPECT_EQ(*braces, "{name}");

    const auto missing = engine::loc::format("Hello {name}", {}, "en");
    ASSERT_TRUE(missing.has_value());
    EXPECT_EQ(*missing, "Hello {name}");

    const auto hash = engine::loc::format("item #1", {}, "en");
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, "item #1");
}

TEST(LocFormat, UkrainianPluralBranches) {
    constexpr std::string_view kPattern =
            "{count, plural, one {# яблуко} few {# яблука} many {# яблук} other {# яблука}}";
    const std::int64_t samples[] = {1, 2, 5, 11, 21, 22};
    const char* expect[] = {"1 яблуко", "2 яблука", "5 яблук", "11 яблук", "21 яблуко", "22 яблука"};
    for (std::size_t i = 0; i < 6; ++i) {
        const engine::loc::Arg args[] = {num("count", samples[i])};
        const auto formatted = engine::loc::format(kPattern, args, "uk");
        ASSERT_TRUE(formatted.has_value()) << samples[i];
        EXPECT_EQ(*formatted, expect[i]) << samples[i];
    }
}

TEST(LocFormat, EnglishPluralAndNestedName) {
    constexpr std::string_view kPattern =
            "{count, plural, one {{name} has # apple} other {{name} has # apples}}";
    const engine::loc::Arg one[] = {num("count", 1), str("name", "Ann")};
    const auto singular = engine::loc::format(kPattern, one, "en");
    ASSERT_TRUE(singular.has_value());
    EXPECT_EQ(*singular, "Ann has 1 apple");

    const engine::loc::Arg many[] = {str("name", "Ann"), num("count", 3)};
    const auto plural = engine::loc::format(kPattern, many, "en");
    ASSERT_TRUE(plural.has_value());
    EXPECT_EQ(*plural, "Ann has 3 apples");
}

TEST(LocFormat, RejectsBrokenPatterns) {
    EXPECT_EQ(engine::loc::format("{count, plural, one {#}}", {}, "en").error(), engine::loc::FormatError::MissingOther);
    EXPECT_EQ(engine::loc::format("{count, plural, one {{count, plural, other {#}}} other {#}}", {}, "en").error(),
            engine::loc::FormatError::NestedPlural);
    EXPECT_EQ(engine::loc::format("{name", {}, "en").error(), engine::loc::FormatError::Unclosed);
    EXPECT_EQ(engine::loc::validate_pattern("{count, plural, one {#} one {#} other {#}}").error(),
            engine::loc::FormatError::DuplicateCategory);
}
