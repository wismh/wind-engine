#include <gtest/gtest.h>

#include <engine/loc/catalog.h>

namespace {

engine::loc::StringTable table_with(std::string locale, std::string id, std::string text) {
    engine::loc::StringTable table;
    table.locale = std::move(locale);
    table.messages.emplace(std::move(id), std::move(text));
    return table;
}

}

TEST(LocCatalog, ParsesTomlAndRejectsBadPattern) {
    const auto parsed = engine::loc::parse_string_table(R"(
locale = "uk"

[[string]]
id = "menu.play"
note = "button"
text = "Грати"

[[string]]
id = "hud.apples"
text = "{count, plural, one {# яблуко} few {# яблука} many {# яблук} other {# яблука}}"
)");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->locale, "uk");
    EXPECT_EQ(parsed->messages.at("menu.play"), "Грати");

    const auto broken = engine::loc::parse_string_table(R"(
locale = "en"
[[string]]
id = "bad"
text = "{count, plural, one {#}}"
)");
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error().kind, engine::loc::StringTableError::BadPattern);

    const auto duplicate = engine::loc::parse_string_table(R"(
locale = "en"
[[string]]
id = "menu.play"
text = "Play"
[[string]]
id = "menu.play"
text = "Start"
)");
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().kind, engine::loc::StringTableError::DuplicateId);

    const auto no_locale = engine::loc::parse_string_table("[[string]]\nid = \"a\"\ntext = \"b\"\n");
    ASSERT_FALSE(no_locale.has_value());
    EXPECT_EQ(no_locale.error().kind, engine::loc::StringTableError::MissingLocale);
}

TEST(LocCatalog, FallbackMissingKeyAndPseudo) {
    engine::loc::StringTable en;
    en.locale = "en";
    en.messages["menu.play"] = "Play";
    en.messages["hud.only_en"] = "Score";

    engine::loc::Catalog catalog;
    catalog.add(std::move(en), engine::loc::Role::Source);
    catalog.add(table_with("uk", "menu.play", "Грати"));
    catalog.set_fallback("en");
    catalog.set_active("uk");

    const engine::loc::Translated play = catalog.text("menu.play");
    EXPECT_EQ(play.text, "Грати");
    EXPECT_FALSE(play.missing_from_source);
    EXPECT_EQ(catalog.warning_count(), 0u);

    const engine::loc::Translated fallback = catalog.text("hud.only_en");
    EXPECT_EQ(fallback.text, "Score");
    EXPECT_FALSE(fallback.missing_from_source);

    const engine::loc::Translated missing = catalog.text("menu.missing");
    EXPECT_EQ(missing.text, "menu.missing");
    EXPECT_TRUE(missing.missing_from_source);

    const std::size_t warnings = catalog.warning_count();
    EXPECT_EQ(catalog.text("menu.missing").text, "menu.missing");
    EXPECT_EQ(catalog.text("hud.only_en").text, "Score");
    EXPECT_EQ(catalog.warning_count(), warnings);

    catalog.set_pseudo(true);
    const engine::loc::Translated pseudo = catalog.text("menu.play");
    EXPECT_EQ(pseudo.text.front(), '[');
    EXPECT_EQ(pseudo.text.back(), ']');
    EXPECT_GT(pseudo.text.size(), std::string("Грати").size());
}

TEST(LocCatalog, SourceKeyWithoutActiveOrFallbackTextStaysTheKey) {
    engine::loc::Catalog catalog;
    catalog.add(table_with("en", "menu.play", "Play"), engine::loc::Role::Source);
    catalog.set_fallback("en");
    catalog.set_active("uk");

    const engine::loc::Translated play = catalog.text("menu.play");
    EXPECT_EQ(play.text, "Play");
    EXPECT_FALSE(play.missing_from_source);

    catalog.set_fallback("");
    const engine::loc::Translated bare = catalog.text("menu.play");
    EXPECT_EQ(bare.text, "menu.play");
    EXPECT_FALSE(bare.missing_from_source);
}
