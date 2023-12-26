#include <gtest/gtest.h>

#include <engine/loc/catalog.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <string>

namespace {

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
    }
};

class ScoreModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> score{2.0f};
    engine::ui::Bindable<std::string> name{"Ann"};

    ScoreModel() {
        property(engine::ui::intern("score"), score);
        property(engine::ui::intern("name"), name);
    }
};

engine::loc::Catalog apples_catalog() {
    engine::loc::StringTable en;
    en.locale = "en";
    en.messages["menu.play"] = "Play";
    en.messages["hud.apples"] = "{count, plural, one {# apple} other {# apples}}";
    en.messages["hud.hello"] = "Hello {name}";

    engine::loc::StringTable uk;
    uk.locale = "uk";
    uk.messages["menu.play"] = "Грати";
    uk.messages["hud.apples"] = "{count, plural, one {# яблуко} few {# яблука} many {# яблук} other {# яблука}}";
    uk.messages["hud.hello"] = "Привіт, {name}";

    engine::loc::Catalog catalog;
    catalog.add(std::move(en), engine::loc::Role::Source);
    catalog.add(std::move(uk));
    catalog.set_fallback("en");
    catalog.set_active("uk");
    return catalog;
}

}

TEST(UiLoc, ApplyBindingsResolvesTrAndFollowsLocale) {
    ScoreModel vm;
    engine::loc::Catalog catalog = apples_catalog();
    auto document = engine::ui::parse_xml(
            R"(<Canvas><Label text="{tr hud.apples count={binding score}}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());

    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "2 яблука");

    vm.score.set(5.0f);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "5 яблук");

    vm.score.set(1.0f);
    catalog.set_active("en");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "1 apple");

    vm.score.set(21.0f);
    catalog.set_active("uk");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "21 яблуко");
}

TEST(UiLoc, MissingSourceKeyIsFatalOnlyWhenHookIsSet) {
    ScoreModel vm;
    engine::loc::Catalog catalog = apples_catalog();
    auto document = engine::ui::parse_xml(R"(<Canvas><Label text="{tr menu.missing}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());

    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "menu.missing");

    RecordingFatalError fatal;
    const auto applied = engine::ui::apply_bindings(*document, vm, &fatal, &catalog);
    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingString);
    EXPECT_EQ(fatal.call_count, 1);
    EXPECT_NE(fatal.last_message.find("menu.missing"), std::string::npos);
}

TEST(UiLoc, MissingArgBindingIsFatal) {
    engine::ui::ViewModel vm;
    engine::loc::Catalog catalog = apples_catalog();
    RecordingFatalError fatal;
    auto document = engine::ui::parse_xml(
            R"(<Canvas><Label text="{tr hud.apples count={binding score}}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());
    const auto applied = engine::ui::apply_bindings(*document, vm, &fatal, &catalog);
    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
}

TEST(UiLoc, StringArgFollowsBinding) {
    ScoreModel vm;
    engine::loc::Catalog catalog = apples_catalog();
    auto document = engine::ui::parse_xml(R"(<Canvas><Label text="{tr hud.hello name={binding name}}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "Привіт, Ann");

    vm.name.set("Іра");
    catalog.set_active("en");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm, nullptr, &catalog).has_value());
    EXPECT_EQ(document->root.children[0].text, "Hello Іра");
}
