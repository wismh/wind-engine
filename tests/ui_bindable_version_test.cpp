#include <gtest/gtest.h>

#include <engine/loc/catalog.h>
#include <engine/ui/bindable.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// wind-228: the versions the bind pass polls.

namespace {

    struct NoCompare {
        int value = 0;
    };

    class VersionVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> name;
        engine::ui::Bindable<float> amount;
        engine::ui::BindableList<std::shared_ptr<VersionVm>> children;

        VersionVm() {
            property(engine::ui::intern("name"), name);
            property(engine::ui::intern("amount"), amount);
            property(engine::ui::intern("children"), children);
        }
    };

    TEST(BindableVersion, SetBumpsOnlyOnARealChange) {
        engine::ui::Bindable<int> value;
        EXPECT_EQ(value.version(), 0u);
        value.set(0);
        EXPECT_EQ(value.version(), 0u) << "the value it already holds";
        value.set(3);
        EXPECT_EQ(value.version(), 1u);
        value.set(3);
        EXPECT_EQ(value.version(), 1u);
        value = 4;
        EXPECT_EQ(value.version(), 2u);
    }

    TEST(BindableVersion, ChangeHandlerAndVersionAgree) {
        engine::ui::Bindable<std::string> text;
        int calls = 0;
        text.on_changed([&](const std::string &) { ++calls; });
        text.set("a");
        text.set("a");
        text.set("b");
        EXPECT_EQ(calls, 2);
        EXPECT_EQ(text.version(), 2u);
    }

    TEST(BindableVersion, ATypeWithoutEqualityAlwaysCountsAsChanged) {
        engine::ui::Bindable<NoCompare> value;
        value.set(NoCompare{1});
        value.set(NoCompare{1});
        EXPECT_EQ(value.version(), 2u);
    }

    TEST(BindableVersion, AListChangesThroughSetAndEditAndBumpsOnADifferenceOnly) {
        engine::ui::BindableList<int> list;
        static_assert(std::is_const_v<std::remove_reference_t<decltype(list.get())>>, "get() is read-only");
        EXPECT_EQ(list.version(), 0u);
        list.set({});
        EXPECT_EQ(list.version(), 0u) << "an empty list set to empty";
        list.set({1, 2, 3});
        EXPECT_EQ(list.version(), 1u);
        list.set({1, 2, 3});
        EXPECT_EQ(list.version(), 1u);
        list.edit([](std::vector<int> &items) { items.push_back(4); });
        EXPECT_EQ(list.version(), 2u);
        list.edit([](std::vector<int> &items) {
            items.push_back(5);
            items.pop_back();
        });
        EXPECT_EQ(list.version(), 2u) << "an edit that ends where it began changes nothing";
        list.edit([](std::vector<int> &items) { items.clear(); });
        EXPECT_EQ(list.version(), 3u);
        EXPECT_TRUE(list.get().empty());
    }

    TEST(BindableVersion, AListOfSharedViewModelsComparesByIdentity) {
        engine::ui::BindableList<std::shared_ptr<VersionVm>> list;
        auto a = std::make_shared<VersionVm>();
        auto b = std::make_shared<VersionVm>();
        list.set({a, b});
        EXPECT_EQ(list.version(), 1u);
        list.set({a, b});
        EXPECT_EQ(list.version(), 1u);
        list.set({b, a});
        EXPECT_EQ(list.version(), 2u) << "a reorder is a change";
    }

    TEST(BindableVersion, TheViewModelExposesTheVersionOfEveryProperty) {
        VersionVm vm;
        const auto name = vm.property_version(engine::ui::intern("name"));
        const auto amount = vm.property_version(engine::ui::intern("amount"));
        const auto children = vm.property_version(engine::ui::intern("children"));
        ASSERT_TRUE(name && amount && children);
        EXPECT_FALSE(vm.property_version(engine::ui::intern("missing")).has_value());

        EXPECT_EQ(name->read(), 0u);
        vm.name.set("x");
        EXPECT_EQ(name->read(), 1u);
        EXPECT_EQ(amount->read(), 0u);
        vm.write_property_float(engine::ui::intern("amount"), 2.0f);
        EXPECT_EQ(amount->read(), 1u) << "a document write takes the same path as a C++ one";
        vm.write_property_float(engine::ui::intern("amount"), 2.0f);
        EXPECT_EQ(amount->read(), 1u);
        vm.children.set({std::make_shared<VersionVm>()});
        EXPECT_EQ(children->read(), 1u);
    }

    TEST(BindableVersion, BindPropertyStringSaysWhetherItWrote) {
        VersionVm vm;
        vm.name.set("hello");
        std::string dest;
        EXPECT_EQ(vm.bind_property_string(engine::ui::intern("name"), dest),
                  engine::ui::ViewModel::StringBind::Changed);
        EXPECT_EQ(dest, "hello");
        EXPECT_EQ(vm.bind_property_string(engine::ui::intern("name"), dest),
                  engine::ui::ViewModel::StringBind::Unchanged);
        EXPECT_EQ(vm.bind_property_string(engine::ui::intern("nope"), dest),
                  engine::ui::ViewModel::StringBind::Missing);
        vm.amount.set(1.5f);
        EXPECT_EQ(vm.bind_property_string(engine::ui::intern("amount"), dest),
                  engine::ui::ViewModel::StringBind::Changed);
        EXPECT_EQ(vm.bind_property_string(engine::ui::intern("amount"), dest),
                  engine::ui::ViewModel::StringBind::Unchanged);
    }

    TEST(CatalogVersion, EveryChangeThatCouldAlterATranslationBumpsIt) {
        engine::loc::Catalog catalog;
        EXPECT_EQ(catalog.version(), 0u);
        engine::loc::StringTable table;
        table.locale = "en";
        table.messages["a"] = "A";
        catalog.add(std::move(table));
        const std::uint64_t after_add = catalog.version();
        EXPECT_GT(after_add, 0u);
        catalog.set_active("en");
        catalog.set_fallback("en");
        catalog.set_pseudo(true);
        EXPECT_EQ(catalog.version(), after_add + 3);
    }

    TEST(StyleAnalysis, ARuleRecordsItsPseudoClassesAndWhatItsDeclarationsDirty) {
        std::vector<std::string> warnings;
        const auto sheet = engine::ui::parse_css(".a { color: #fff; }\n"
                                                 ".a:hover { background: #000; }\n"
                                                 ".a:hover { padding: 4; color: #f00; }\n"
                                                 ".panel:hover .b:pressed { width: 10; }\n"
                                                 ".c { --x: 3; }\n"
                                                 ".d:focus { color: var(--x); }\n",
                                                 warnings);
        ASSERT_TRUE(sheet.has_value());
        ASSERT_EQ(sheet->rules.size(), 6u);
        const auto &rules = sheet->rules;
        using namespace engine::ui;
        EXPECT_EQ(rules[0].subject_pseudo | rules[0].ancestor_pseudo, 0);
        EXPECT_EQ(rules[0].dirties, kDirtyStyle | kDirtyPaint);
        EXPECT_EQ(rules[1].subject_pseudo, kPseudoHover);
        EXPECT_EQ(rules[1].dirties, kDirtyStyle | kDirtyPaint) << "a paint-only rule";
        EXPECT_EQ(rules[2].dirties, kDirtyStyle | kDirtyLayout | kDirtyPaint);
        EXPECT_EQ(rules[3].subject_pseudo, kPseudoPressed);
        EXPECT_EQ(rules[3].ancestor_pseudo, kPseudoHover) << "on the ancestor compound";
        EXPECT_EQ(rules[3].dirties, kDirtyStyle | kDirtyLayout);
        EXPECT_EQ(rules[4].dirties, kDirtyStyle | kDirtyLayout | kDirtyPaint) << "a custom property feeds anything";
        EXPECT_EQ(rules[5].dirties, kDirtyStyle | kDirtyLayout | kDirtyPaint) << "so does a var()";
        EXPECT_EQ(rules[0].line, 1u);
        EXPECT_EQ(rules[3].line, 4u);
        EXPECT_EQ(rules[5].line, 6u);

        const StyleAnalysis &analysis = style_analysis(*sheet);
        EXPECT_EQ(analysis.pseudo_rules, (std::vector<std::uint32_t>{1, 2, 3, 5}));
        EXPECT_EQ(analysis.layout_pseudo_rules, (std::vector<std::uint32_t>{2, 3, 5}));
        EXPECT_EQ(analysis.ancestor_pseudo_mask, kPseudoHover);
        EXPECT_EQ(analysis.layout_pseudo[1], 0);
        EXPECT_EQ(analysis.layout_pseudo[2], 1);
    }

    TEST(StyleAnalysis, ASheetEditedAfterwardsIsAnalysedAgainWhenItsRuleCountChanges) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(".a:hover { color: #fff; }", warnings);
        ASSERT_TRUE(sheet.has_value());
        EXPECT_EQ(engine::ui::style_analysis(*sheet).layout_pseudo_rules.size(), 0u);
        engine::ui::CssRule rule = sheet->rules[0];
        rule.declarations = {engine::ui::CssDeclaration{"padding", "4"}};
        sheet->rules.push_back(rule);
        EXPECT_EQ(engine::ui::style_analysis(*sheet).layout_pseudo_rules.size(), 1u) << "analysed from the selectors";
    }

} // namespace
