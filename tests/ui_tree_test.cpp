#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/core/key_code.h>
#include <engine/render/commands.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/tree.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// engine/ui/tree.h: flatten_tree, TreeExpansion, tree_navigate, and the row recipe from
// docs/tech/modules/UI.md#trees (a virtualized ItemsControl of flat rows, indented by `var-depth`).

namespace {

struct TestNode {
    int id = 0;
    std::vector<TestNode> children;
};

struct TestSource {
    [[nodiscard]] int key(const TestNode *node) const { return node->id; }
    [[nodiscard]] bool has_children(const TestNode *node) const { return !node->children.empty(); }

    template<typename Emit>
    void for_each_child(const TestNode *node, Emit &&emit) const {
        ++*children_made;
        for (const TestNode &child: node->children) {
            emit(&child);
        }
    }

    int *children_made = nullptr;
};

// 1
// ├─ 2
// │  ├─ 4
// │  └─ 5
// └─ 3
// 6
[[nodiscard]] std::vector<TestNode> make_forest() {
    TestNode two{2, {TestNode{4, {}}, TestNode{5, {}}}};
    TestNode one{1, {std::move(two), TestNode{3, {}}}};
    return {std::move(one), TestNode{6, {}}};
}

[[nodiscard]] std::vector<const TestNode *> roots_of(const std::vector<TestNode> &forest) {
    std::vector<const TestNode *> roots;
    for (const TestNode &node: forest) {
        roots.push_back(&node);
    }
    return roots;
}

struct Flat {
    std::vector<engine::ui::TreeRowInfo> infos;
    std::vector<int> ids;
    int children_made = 0;
};

[[nodiscard]] Flat flatten(const std::vector<TestNode> &forest, const engine::ui::TreeExpansion<int> &expansion) {
    Flat flat;
    TestSource source{&flat.children_made};
    flat.infos = engine::ui::flatten_tree(roots_of(forest), expansion, source,
                                          [&flat](const TestNode *node, const engine::ui::TreeRowInfo &) {
                                              flat.ids.push_back(node->id);
                                          });
    return flat;
}

} // namespace

TEST(UiTree, FlattensDepthFirstWithDepthParentAndChildren) {
    const std::vector<TestNode> forest = make_forest();
    const Flat flat = flatten(forest, engine::ui::TreeExpansion<int>{});

    EXPECT_EQ(flat.ids, (std::vector<int>{1, 2, 4, 5, 3, 6}));
    ASSERT_EQ(flat.infos.size(), 6u);
    EXPECT_EQ(flat.infos[0], (engine::ui::TreeRowInfo{0, true, true, engine::ui::kNoTreeRow}));
    EXPECT_EQ(flat.infos[1], (engine::ui::TreeRowInfo{1, true, true, 0}));
    EXPECT_EQ(flat.infos[2], (engine::ui::TreeRowInfo{2, false, false, 1}));
    EXPECT_EQ(flat.infos[3], (engine::ui::TreeRowInfo{2, false, false, 1}));
    EXPECT_EQ(flat.infos[4], (engine::ui::TreeRowInfo{1, false, false, 0}));
    EXPECT_EQ(flat.infos[5], (engine::ui::TreeRowInfo{0, false, false, engine::ui::kNoTreeRow}));
}

TEST(UiTree, CollapsedNodeHidesItsSubtreeAndIsNotWalked) {
    const std::vector<TestNode> forest = make_forest();
    engine::ui::TreeExpansion<int> expansion;
    expansion.set_expanded(2, false);
    const Flat flat = flatten(forest, expansion);

    EXPECT_EQ(flat.ids, (std::vector<int>{1, 2, 3, 6}));
    EXPECT_TRUE(flat.infos[1].has_children);
    EXPECT_FALSE(flat.infos[1].expanded);
    EXPECT_EQ(flat.infos[2].parent, 0u) << "3 keeps 1 as its parent row after 2's children are gone";
    EXPECT_EQ(flat.children_made, 1) << "only 1 is asked for its children";

    expansion.set_expanded(1, false);
    EXPECT_EQ(flatten(forest, expansion).ids, (std::vector<int>{1, 6}));
}

TEST(UiTree, CollapsedByDefaultShowsOnlyRootsUntilExpanded) {
    const std::vector<TestNode> forest = make_forest();
    engine::ui::TreeExpansion<int> expansion(false);
    EXPECT_FALSE(expansion.expanded_by_default());
    EXPECT_EQ(flatten(forest, expansion).ids, (std::vector<int>{1, 6}));

    expansion.toggle(1);
    EXPECT_EQ(flatten(forest, expansion).ids, (std::vector<int>{1, 2, 3, 6}));
    expansion.toggle(2);
    EXPECT_EQ(flatten(forest, expansion).ids, (std::vector<int>{1, 2, 4, 5, 3, 6}));
}

TEST(UiTree, ExpansionTogglesRetainsAndResets) {
    engine::ui::TreeExpansion<int> expansion;
    EXPECT_TRUE(expansion.is_expanded(7));
    expansion.toggle(7);
    EXPECT_FALSE(expansion.is_expanded(7));
    expansion.toggle(7);
    EXPECT_TRUE(expansion.is_expanded(7));

    expansion.set_expanded(1, false);
    expansion.set_expanded(2, false);
    expansion.retain([](int key) { return key != 1; });
    EXPECT_TRUE(expansion.is_expanded(1)) << "a dropped key goes back to the default";
    EXPECT_FALSE(expansion.is_expanded(2));

    expansion.reset();
    EXPECT_TRUE(expansion.is_expanded(2));
}

TEST(UiTree, NavigateMovesUpDownFirstLastAndClamps) {
    const Flat flat = flatten(make_forest(), engine::ui::TreeExpansion<int>{});
    using engine::ui::TreeNav;
    using engine::ui::TreeNavResult;
    const auto nav = [&flat](std::size_t current, TreeNav key) {
        return engine::ui::tree_navigate(flat.infos, current, key);
    };

    EXPECT_EQ(nav(2, TreeNav::Up), TreeNavResult{1});
    EXPECT_EQ(nav(2, TreeNav::Down), TreeNavResult{3});
    EXPECT_EQ(nav(0, TreeNav::Up), TreeNavResult{0});
    EXPECT_EQ(nav(5, TreeNav::Down), TreeNavResult{5});
    EXPECT_EQ(nav(3, TreeNav::First), TreeNavResult{0});
    EXPECT_EQ(nav(3, TreeNav::Last), TreeNavResult{5});
}

TEST(UiTree, NavigateLeftCollapsesThenClimbsAndRightExpandsThenDescends) {
    const std::vector<TestNode> forest = make_forest();
    engine::ui::TreeExpansion<int> expansion;
    expansion.set_expanded(2, false);
    const Flat flat = flatten(forest, expansion); // 1, 2 (collapsed), 3, 6
    using engine::ui::TreeNav;
    using engine::ui::TreeNavResult;
    const auto nav = [&flat](std::size_t current, TreeNav key) {
        return engine::ui::tree_navigate(flat.infos, current, key);
    };

    EXPECT_EQ(nav(0, TreeNav::Left), (TreeNavResult{0, true})) << "an expanded row collapses";
    EXPECT_EQ(nav(1, TreeNav::Left), TreeNavResult{0}) << "a collapsed row moves to its parent";
    EXPECT_EQ(nav(2, TreeNav::Left), TreeNavResult{0}) << "a leaf moves to its parent";
    EXPECT_EQ(nav(3, TreeNav::Left), TreeNavResult{3}) << "a root leaf stays";

    EXPECT_EQ(nav(1, TreeNav::Right), (TreeNavResult{1, true})) << "a collapsed row expands";
    EXPECT_EQ(nav(0, TreeNav::Right), TreeNavResult{1}) << "an expanded row moves to its first child";
    EXPECT_EQ(nav(2, TreeNav::Right), TreeNavResult{2}) << "a leaf stays";
}

TEST(UiTree, NavigateWithoutSelectionOrRows) {
    const Flat flat = flatten(make_forest(), engine::ui::TreeExpansion<int>{});
    using engine::ui::kNoTreeRow;
    using engine::ui::TreeNav;
    using engine::ui::TreeNavResult;

    EXPECT_EQ(engine::ui::tree_navigate(flat.infos, kNoTreeRow, TreeNav::Down), TreeNavResult{0});
    EXPECT_EQ(engine::ui::tree_navigate(flat.infos, kNoTreeRow, TreeNav::Up), TreeNavResult{5});
    EXPECT_EQ(engine::ui::tree_navigate(flat.infos, kNoTreeRow, TreeNav::Right), TreeNavResult{});
    EXPECT_EQ(engine::ui::tree_navigate({}, 0, TreeNav::Down), TreeNavResult{});
}

TEST(UiTree, ArrowsHomeAndEndAreTreeKeys) {
    using engine::KeyCode;
    using engine::ui::TreeNav;
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::Up), TreeNav::Up);
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::Down), TreeNav::Down);
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::Left), TreeNav::Left);
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::Right), TreeNav::Right);
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::Home), TreeNav::First);
    EXPECT_EQ(engine::ui::tree_nav_for_key(KeyCode::End), TreeNav::Last);
    EXPECT_FALSE(engine::ui::tree_nav_for_key(KeyCode::Space).has_value());
}

namespace {

class TreeRowVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<int> depth;

    TreeRowVm() { property(engine::ui::intern("depth"), depth); }
};

class TreeListVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<TreeRowVm>> rows;

    TreeListVm() { property(engine::ui::intern("rows"), rows); }
};

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    return sheet.value_or(engine::ui::Stylesheet{});
}

void run_frame(engine::ui::UiDocument &doc, engine::ui::ViewModel &vm, const engine::ui::Stylesheet &sheet) {
    ASSERT_TRUE(engine::ui::apply_bindings(doc, vm).has_value());
    engine::ui::apply_layout_style(doc.root, &sheet, 800.0f, 600.0f);
    engine::ui::layout(doc, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});
}

constexpr char kTreeXml[] = R"(
    <Canvas>
      <ItemsControl class="list" items_source="{binding rows}">
        <ItemTemplate>
          <Stack class="row" direction="horizontal" var-depth="{binding depth}">
            <Canvas class="mark"/>
          </Stack>
        </ItemTemplate>
      </ItemsControl>
    </Canvas>
)";

constexpr char kTreeCss[] = R"(
    .list { overflow-y: auto; height: 100px; width: 400px; }
    .row { height: 20px; width: 100%; padding: 0 0 0 calc(var(--depth, 0) * 14px); }
    .mark { width: 10px; height: 10px; }
)";

// A 10k-node forest: 100 roots of 100 leaves each.
[[nodiscard]] std::vector<TestNode> make_wide_forest() {
    std::vector<TestNode> forest;
    int next = 0;
    for (int root = 0; root < 100; ++root) {
        TestNode node{next++, {}};
        for (int leaf = 0; leaf < 99; ++leaf) {
            node.children.push_back(TestNode{next++, {}});
        }
        forest.push_back(std::move(node));
    }
    return forest;
}

[[nodiscard]] std::vector<std::shared_ptr<TreeRowVm>>
row_view_models(const std::vector<TestNode> &forest, const engine::ui::TreeExpansion<int> &expansion) {
    std::vector<std::shared_ptr<TreeRowVm>> rows;
    int children_made = 0;
    TestSource source{&children_made};
    (void) engine::ui::flatten_tree(roots_of(forest), expansion, source,
                                    [&rows](const TestNode *, const engine::ui::TreeRowInfo &info) {
                                        auto row = std::make_shared<TreeRowVm>();
                                        row->depth.set(info.depth);
                                        rows.push_back(std::move(row));
                                    });
    return rows;
}

[[nodiscard]] std::size_t real_rows(const engine::ui::Element &items_control) {
    std::size_t count = 0;
    for (const engine::ui::Element &child: items_control.generated_items) {
        if (child.generated_owner != nullptr) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST(UiTree, DepthBindingIndentsRowThroughCalcOfVar) {
    const std::vector<TestNode> forest = make_forest();
    TreeListVm vm;
    vm.rows.set(row_view_models(forest, engine::ui::TreeExpansion<int>{}));
    auto parsed = engine::ui::parse_xml(kTreeXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kTreeCss);
    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);

    const engine::ui::Element &list = parsed->root.children[0];
    ASSERT_GE(real_rows(list), 3u);
    const engine::ui::Element *rows[3] = {};
    std::size_t found = 0;
    for (const engine::ui::Element &child: list.generated_items) {
        if (child.generated_owner != nullptr && found < 3) {
            rows[found++] = &child;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const engine::ui::Element &mark = rows[i]->children[0];
        EXPECT_FLOAT_EQ(mark.layout_rect.x - rows[i]->layout_rect.x, 14.0f * static_cast<float>(i))
                << "row " << i << " sits at depth " << i;
    }
}

TEST(UiTree, CollapsedBranchAddsNothingToTheVirtualizedWindow) {
    const std::vector<TestNode> forest = make_wide_forest();
    engine::ui::TreeExpansion<int> expansion;
    TreeListVm vm;
    vm.rows.set(row_view_models(forest, expansion));
    ASSERT_EQ(vm.rows.get().size(), 10'000u);

    auto parsed = engine::ui::parse_xml(kTreeXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kTreeCss);
    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const std::size_t window = real_rows(parsed->root.children[0]);
    EXPECT_LT(window, 50u) << "100px of 20px rows plus overscan, not 10k rows";

    for (const TestNode &root: forest) {
        expansion.set_expanded(root.id, false);
    }
    vm.rows.set(row_view_models(forest, expansion));
    EXPECT_EQ(vm.rows.get().size(), 100u) << "a collapsed branch is not a row";
    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    EXPECT_LE(real_rows(parsed->root.children[0]), window);
}

TEST(UiTree, VarSubstitutesInsideCalcAndInsets) {
    class DepthVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> depth;
        DepthVm() { property(engine::ui::intern("depth"), depth); }
    };
    DepthVm vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Stack class="row" direction="horizontal" var-depth="{binding depth}">)"
            R"(<Canvas class="mark"/></Stack></Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .row { --step: 10px; width: 400px; height: 20px; padding: 0 0 0 calc(var(--depth, 1) * var(--step)); }
        .mark { width: 10px; height: 10px; }
    )");
    const auto indent = [&] {
        run_frame(*parsed, vm, sheet);
        const engine::ui::Element &row = parsed->root.children[0];
        return row.children[0].layout_rect.x - row.layout_rect.x;
    };

    vm.depth.set("3");
    EXPECT_FLOAT_EQ(indent(), 30.0f) << "a bound value and a cascaded one in one calc()";
    vm.depth.set("0");
    EXPECT_FLOAT_EQ(indent(), 0.0f);
}

TEST(UiTree, ScrollItemIntoViewReachesARowTheWindowHasNotBuilt) {
    const std::vector<TestNode> forest = make_wide_forest();
    TreeListVm vm;
    vm.rows.set(row_view_models(forest, engine::ui::TreeExpansion<int>{}));
    auto parsed = engine::ui::parse_xml(kTreeXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kTreeCss);
    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);

    engine::ui::Element &list = parsed->root.children[0];
    const void *target = vm.rows.get()[5'000].get();
    ASSERT_EQ(engine::ui::find_by_generated_owner(parsed->root, target), nullptr) << "row 5000 is off the window";

    ASSERT_TRUE(engine::ui::scroll_item_into_view(list, list, 5'000));
    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const engine::ui::Element *row = engine::ui::find_by_generated_owner(parsed->root, target);
    ASSERT_NE(row, nullptr);
    const float view_top = list.layout_rect.y + list.scroll_y;
    EXPECT_GE(row->layout_rect.y, view_top);
    EXPECT_LE(row->layout_rect.y + row->layout_rect.h, view_top + list.layout_rect.h) << "scrolled to the bottom edge";

    const float scrolled = list.scroll_y;
    ASSERT_TRUE(engine::ui::scroll_item_into_view(list, list, 5'000));
    EXPECT_FLOAT_EQ(list.scroll_y, scrolled) << "a visible row does not move the list";

    ASSERT_TRUE(engine::ui::scroll_item_into_view(list, list, 0));
    EXPECT_FLOAT_EQ(list.scroll_y, 0.0f);
    EXPECT_FALSE(engine::ui::scroll_item_into_view(list, list, 10'000)) << "past the last item";
}

TEST(UiTree, FindByIdSkipsTemplates) {
    TreeListVm vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><ItemsControl id="rows" items_source="{binding rows}">)"
            R"(<ItemTemplate><Stack id="inner"/></ItemTemplate></ItemsControl></Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element *rows = engine::ui::find_by_id(parsed->root, "rows");
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->kind, engine::ui::ElementKind::ItemsControl);
    EXPECT_EQ(engine::ui::find_by_id(parsed->root, "inner"), nullptr);
    EXPECT_EQ(engine::ui::find_by_id(parsed->root, "missing"), nullptr);
}
