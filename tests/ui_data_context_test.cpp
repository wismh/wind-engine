#include <gtest/gtest.h>

#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fixtures/bind_error_fakes.h"
#include "ui/bind_scan.h"

// `data-context="{binding child}"`: an element and its subtree bind against a child view-model.

namespace {

using engine::ui::Element;

Element* find_class(Element& root, std::string_view class_name) {
    if (std::ranges::find(root.classes, class_name) != root.classes.end()) {
        return &root;
    }
    for (Element& child : root.children) {
        if (Element* found = find_class(child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

class ItemViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> name;

    explicit ItemViewModel(std::string value) : name(std::move(value)) {
        property(engine::ui::intern("name"), name);
    }
};

class InventoryViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title{"inventory"};
    engine::ui::Bindable<bool> open{true};
    engine::ui::BindableList<std::shared_ptr<ItemViewModel>> items;
    int clicks = 0;
    engine::ui::RelayCommand close{[this] { ++clicks; }};

    InventoryViewModel() {
        property(engine::ui::intern("title"), title);
        property(engine::ui::intern("open"), open);
        property(engine::ui::intern("items"), items);
        command(engine::ui::intern("close"), close);
    }
};

class ScreenViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title{"screen"};
    engine::ui::Bindable<std::shared_ptr<InventoryViewModel>> inventory{std::make_shared<InventoryViewModel>()};

    ScreenViewModel() {
        property(engine::ui::intern("title"), title);
        property(engine::ui::intern("inventory"), inventory);
    }
};

constexpr std::string_view kScreenXml = R"(<Canvas>
<Label class="screen-title" text="{binding title}"/>
<Stack class="panel" data-context="{binding inventory}">
  <Label class="panel-title" text="{binding title}"/>
  <Button class="close" command="{binding close}"/>
</Stack>
</Canvas>)";

std::string text_of(engine::ui::UiDocument& document, std::string_view class_name) {
    const Element* element = find_class(document.root, class_name);
    return element != nullptr ? element->text : std::string("<missing>");
}

}

TEST(DataContext, SubtreeBindsAgainstTheChildAndTheRestAgainstTheRoot) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    EXPECT_EQ(text_of(*document, "screen-title"), "screen");
    EXPECT_EQ(text_of(*document, "panel-title"), "inventory");
}

TEST(DataContext, ChildChangesReachTheDocument) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    screen.inventory.get()->title.set("bag");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    EXPECT_EQ(text_of(*document, "panel-title"), "bag");
    EXPECT_EQ(text_of(*document, "screen-title"), "screen");
}

TEST(DataContext, ReplacingTheChildBindsTheSubtreeAgain) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    auto old_child = screen.inventory.get();
    auto next = std::make_shared<InventoryViewModel>();
    next->title.set("chest");
    screen.inventory.set(next);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    EXPECT_EQ(text_of(*document, "panel-title"), "chest");

    // The old child is no longer read.
    old_child->title.set("stale");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    EXPECT_EQ(text_of(*document, "panel-title"), "chest");
}

TEST(DataContext, NullChildHidesTheElementAndANewOneShowsItAgain) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    Element* panel = find_class(document->root, "panel");
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->hidden_by_scope);

    screen.inventory.set(nullptr);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    EXPECT_TRUE(panel->hidden_by_scope);

    auto next = std::make_shared<InventoryViewModel>();
    next->title.set("again");
    screen.inventory.set(next);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());
    EXPECT_FALSE(panel->hidden_by_scope);
    EXPECT_EQ(text_of(*document, "panel-title"), "again");
}

TEST(DataContext, SubtreeOwnerIsTheChildSoCommandsReachIt) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    Element* panel = find_class(document->root, "panel");
    Element* close = find_class(document->root, "close");
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(close, nullptr);
    const InventoryViewModel* child = screen.inventory.get().get();
    EXPECT_EQ(panel->generated_owner, child);
    EXPECT_EQ(close->generated_owner, child);
    ASSERT_NE(close->command, nullptr);
    close->command->execute();
    EXPECT_EQ(screen.inventory.get()->clicks, 1);

    // The element outside the scope is not owned by it.
    EXPECT_EQ(find_class(document->root, "screen-title")->generated_owner, nullptr);
}

TEST(DataContext, ReplacingTheChildReStampsTheOwnerOfTheWholeSubtree) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(kScreenXml, nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    auto next = std::make_shared<InventoryViewModel>();
    screen.inventory.set(next);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    EXPECT_EQ(find_class(document->root, "close")->generated_owner, next.get());
    EXPECT_EQ(find_class(document->root, "panel-title")->generated_owner, next.get());
    find_class(document->root, "close")->command->execute();
    EXPECT_EQ(next->clicks, 1);
}

TEST(DataContext, ItemsControlInsideTheScopeReadsTheChildsList) {
    ScreenViewModel screen;
    screen.inventory.get()->items.set({std::make_shared<ItemViewModel>("sword"), std::make_shared<ItemViewModel>("shield")});
    auto document = engine::ui::parse_xml(R"(<Canvas>
<Stack data-context="{binding inventory}">
  <ItemsControl class="list" items_source="{binding items}">
    <ItemTemplate><Label class="row" text="{binding name}"/></ItemTemplate>
  </ItemsControl>
</Stack>
</Canvas>)", nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, screen).has_value());

    Element* list = find_class(document->root, "list");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->generated_items.size(), 2u);
    EXPECT_EQ(list->generated_items[0].text, "sword");
    EXPECT_EQ(list->generated_items[1].text, "shield");
}

TEST(DataContext, ScopesNest) {
    class OuterViewModel final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::shared_ptr<InventoryViewModel>> bag{std::make_shared<InventoryViewModel>()};
        OuterViewModel() { property(engine::ui::intern("bag"), bag); }
    };
    class RootViewModel final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::shared_ptr<OuterViewModel>> outer{std::make_shared<OuterViewModel>()};
        RootViewModel() { property(engine::ui::intern("outer"), outer); }
    };
    RootViewModel root;
    root.outer.get()->bag.get()->title.set("deep");
    auto document = engine::ui::parse_xml(R"(<Canvas>
<Stack data-context="{binding outer}">
  <Stack data-context="{binding bag}"><Label class="deep" text="{binding title}"/></Stack>
</Stack>
</Canvas>)", nullptr, &root);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, root).has_value());
    EXPECT_EQ(text_of(*document, "deep"), "deep");
    EXPECT_EQ(find_class(document->root, "deep")->generated_owner, root.outer.get()->bag.get().get());
}

TEST(DataContext, MissingScopeIsReportedAndHidesTheElement) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(R"(<Canvas><Stack class="panel" data-context="{binding nope}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());
    fakes::RecordingBindErrors errors;
    EXPECT_FALSE(engine::ui::apply_bindings(*document, screen, &errors).has_value());
    EXPECT_GE(errors.call_count, 1);
    EXPECT_TRUE(find_class(document->root, "panel")->hidden_by_scope);
}

TEST(DataContext, PropertyThatIsNotAViewModelIsReportedAsMistyped) {
    ScreenViewModel screen;
    auto document = engine::ui::parse_xml(R"(<Canvas><Stack class="panel" data-context="{binding title}"/></Canvas>)");
    ASSERT_TRUE(document.has_value());
    fakes::RecordingBindErrors errors;
    EXPECT_FALSE(engine::ui::apply_bindings(*document, screen, &errors).has_value());
    EXPECT_NE(errors.last_message.find("data-context"), std::string::npos);
}

TEST(DataContext, ParseChecksTheScopeNameAgainstTheRootAndNothingBelowIt) {
    ScreenViewModel screen;
    EXPECT_FALSE(engine::ui::parse_xml(R"(<Canvas><Stack data-context="{binding nope}"/></Canvas>)", nullptr, &screen)
                         .has_value());
    // `close` is registered on the child, not on the root: below the scope it is not checked against the root.
    EXPECT_TRUE(engine::ui::parse_xml(R"(<Canvas><Stack data-context="{binding inventory}">
<Button command="{binding close}"/></Stack></Canvas>)", nullptr, &screen)
                        .has_value());
}

TEST(DataContext, RootOfAnItemRowCannotCarryAScope) {
    ScreenViewModel screen;
    screen.inventory.get()->items.set({std::make_shared<ItemViewModel>("sword")});
    auto document = engine::ui::parse_xml(R"(<Canvas>
<Stack data-context="{binding inventory}">
  <ItemsControl class="list" items_source="{binding items}">
    <ItemTemplate><Label class="row" data-context="{binding name}" text="{binding name}"/></ItemTemplate>
  </ItemsControl>
</Stack>
</Canvas>)", nullptr, &screen);
    ASSERT_TRUE(document.has_value());
    fakes::RecordingBindErrors errors;
    EXPECT_FALSE(engine::ui::apply_bindings(*document, screen, &errors).has_value());
    EXPECT_NE(errors.last_message.find("ItemTemplate row"), std::string::npos);
}

TEST(DataContext, ScanPutsTheSubtreeInTheChildsBinder) {
    const auto binder = engine::ui::scan_bind_tree(kScreenXml);
    ASSERT_TRUE(binder.has_value());

    const auto has = [](const engine::ui::BindBinder& b, std::string_view path) {
        return std::ranges::any_of(b.members, [&](const engine::ui::BindMember& m) { return m.path == path; });
    };
    EXPECT_TRUE(has(*binder, "title"));
    EXPECT_TRUE(has(*binder, "inventory"));
    EXPECT_FALSE(has(*binder, "close"));
    ASSERT_EQ(binder->nested.size(), 1u);
    EXPECT_EQ(binder->nested[0].first, "inventory");
    EXPECT_TRUE(has(binder->nested[0].second, "title"));
    EXPECT_TRUE(has(binder->nested[0].second, "close"));
}
