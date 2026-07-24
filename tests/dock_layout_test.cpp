#include <gtest/gtest.h>

#include <engine/render/commands.h>
#include <engine/ui/dock_geometry.h>
#include <engine/ui/dock_layout.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// engine/ui/dock_layout.h and dock_geometry.h: the dock model, its geometry, drop and chrome hit tests, text, and
// reconcile. docs/tech/features/Docking.md.

namespace {

using engine::render::Rect;
using engine::ui::compute_dock_geometry;
using engine::ui::DockAxis;
using engine::ui::DockChromeHit;
using engine::ui::DockChromeKind;
using engine::ui::DockDrop;
using engine::ui::DockDropKind;
using engine::ui::DockFloat;
using engine::ui::DockGeometry;
using engine::ui::DockLayout;
using engine::ui::DockMetrics;
using engine::ui::DockNode;
using engine::ui::DockNodeId;
using engine::ui::DockNodeKind;
using engine::ui::DockResizeEdges;
using engine::ui::DockSpot;
using engine::ui::DockTarget;
using engine::ui::DockZone;
using engine::ui::kNoDockNode;

constexpr Rect kArea{0.0f, 0.0f, 800.0f, 600.0f};

[[nodiscard]] DockNodeId stack_of(const DockLayout &layout, std::string_view key) {
    const auto place = layout.find(key);
    return place ? place->stack : kNoDockNode;
}

[[nodiscard]] const DockNode &root_node(const DockLayout &layout) { return *layout.node(layout.root()); }

[[nodiscard]] std::vector<std::string> keys(std::initializer_list<const char *> list) {
    return {list.begin(), list.end()};
}

// a | b, side by side at 0.5.
[[nodiscard]] DockLayout side_by_side() {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    return layout;
}

} // namespace

// --- Model -----------------------------------------------------------------------------------------------------

TEST(DockLayout, StartsEmptyAndValid) {
    const DockLayout layout;
    EXPECT_TRUE(layout.empty());
    EXPECT_EQ(layout.root(), kNoDockNode);
    EXPECT_TRUE(layout.valid());
    EXPECT_FALSE(layout.find("a").has_value());
    EXPECT_FALSE(layout.is_visible("a"));
    EXPECT_TRUE(layout.panels().empty());
}

TEST(DockLayout, AddIntoEmptyRootMakesAStack) {
    DockLayout layout;
    ASSERT_TRUE(layout.add("a", {kNoDockNode, DockZone::Left}));
    const DockNode &root = root_node(layout);
    EXPECT_EQ(root.kind, DockNodeKind::Tabs);
    EXPECT_EQ(root.panels, keys({"a"}));
    EXPECT_EQ(root.parent, kNoDockNode);
    EXPECT_TRUE(layout.is_visible("a"));
    EXPECT_EQ(layout.find("a")->float_id, kNoDockNode);
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, CenterJoinsTheStackAndActivatesTheNewTab) {
    DockLayout layout;
    layout.add("a", {});
    ASSERT_TRUE(layout.add("b", {kNoDockNode, DockZone::Center}));
    ASSERT_TRUE(layout.add("c", {layout.root(), DockZone::Center}));
    EXPECT_EQ(root_node(layout).panels, keys({"a", "b", "c"}));
    EXPECT_EQ(root_node(layout).active, 2u);
    EXPECT_FALSE(layout.is_visible("a"));
    EXPECT_TRUE(layout.is_visible("c"));
    EXPECT_EQ(layout.find("b")->index, 1u);
}

TEST(DockLayout, EdgeZonesSplitTheTargetOnThatSide) {
    struct Case {
        DockZone zone;
        DockAxis axis;
        bool new_first;
    };
    for (const Case c: {Case{DockZone::Left, DockAxis::Horizontal, true},
                        Case{DockZone::Right, DockAxis::Horizontal, false},
                        Case{DockZone::Top, DockAxis::Vertical, true},
                        Case{DockZone::Bottom, DockAxis::Vertical, false}}) {
        DockLayout layout;
        layout.add("a", {});
        const DockNodeId a = stack_of(layout, "a");
        ASSERT_TRUE(layout.add("b", {a, c.zone}));
        const DockNode &root = root_node(layout);
        EXPECT_EQ(root.kind, DockNodeKind::Split);
        EXPECT_EQ(root.axis, c.axis);
        EXPECT_FLOAT_EQ(root.ratio, 0.5f);
        const DockNodeId b = stack_of(layout, "b");
        EXPECT_EQ(root.first, c.new_first ? b : a);
        EXPECT_EQ(root.second, c.new_first ? a : b);
        EXPECT_EQ(layout.node(a)->parent, layout.root());
        EXPECT_EQ(layout.node(b)->parent, layout.root());
        EXPECT_TRUE(layout.valid());
    }
}

TEST(DockLayout, RootEdgeSplitsTheWholeDockedTree) {
    DockLayout layout = side_by_side();
    const DockNodeId old_root = layout.root();
    ASSERT_TRUE(layout.add("c", {kNoDockNode, DockZone::Bottom}));
    const DockNode &root = root_node(layout);
    EXPECT_EQ(root.axis, DockAxis::Vertical);
    EXPECT_EQ(root.first, old_root);
    EXPECT_EQ(root.second, stack_of(layout, "c"));
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, InvalidAddsChangeNothing) {
    DockLayout layout = side_by_side();
    const DockLayout before = layout;
    EXPECT_FALSE(layout.add("a", {kNoDockNode, DockZone::Left}));        // duplicate
    EXPECT_FALSE(layout.add("", {kNoDockNode, DockZone::Left}));         // empty key
    EXPECT_FALSE(layout.add("c", {999, DockZone::Left}));                // unknown node
    EXPECT_FALSE(layout.add("c", {layout.root(), DockZone::Center}));    // Center on a split
    EXPECT_FALSE(layout.add("c", {kNoDockNode, DockZone::Center}));      // Center on a split root
    EXPECT_EQ(layout, before);
}

TEST(DockLayout, RemoveKeepsANeighbourActive) {
    DockLayout layout;
    for (const char *key: {"a", "b", "c"}) {
        layout.add(key, {});
    }
    ASSERT_TRUE(layout.is_visible("c"));
    ASSERT_TRUE(layout.remove("c"));
    EXPECT_TRUE(layout.is_visible("b"));
    layout.add("c", {});
    layout.activate("b");
    ASSERT_TRUE(layout.remove("a"));
    EXPECT_TRUE(layout.is_visible("b"));
    EXPECT_EQ(root_node(layout).panels, keys({"b", "c"}));
    ASSERT_TRUE(layout.remove("b"));
    EXPECT_TRUE(layout.is_visible("c"));
    EXPECT_FALSE(layout.remove("missing"));
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, RemovingTheLastTabCollapsesTheSplit) {
    DockLayout layout = side_by_side();
    const DockNodeId b = stack_of(layout, "b");
    ASSERT_TRUE(layout.remove("a"));
    EXPECT_EQ(layout.root(), b);
    EXPECT_EQ(layout.node(b)->parent, kNoDockNode);
    EXPECT_EQ(layout.nodes().size(), 1u);
    ASSERT_TRUE(layout.remove("b"));
    EXPECT_TRUE(layout.empty());
    EXPECT_TRUE(layout.nodes().empty());
}

TEST(DockLayout, RemoveInsideANestedSplitReparentsTheSurvivor) {
    DockLayout layout = side_by_side();
    const DockNodeId root = layout.root();
    layout.add("c", {stack_of(layout, "b"), DockZone::Bottom});
    ASSERT_TRUE(layout.remove("b"));
    EXPECT_EQ(layout.root(), root);
    const DockNodeId c = stack_of(layout, "c");
    EXPECT_EQ(root_node(layout).second, c);
    EXPECT_EQ(layout.node(c)->parent, root);
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, MoveJoinsAnotherStackAndCollapsesTheSource) {
    DockLayout layout = side_by_side();
    const DockNodeId b = stack_of(layout, "b");
    ASSERT_TRUE(layout.move("a", {b, DockZone::Center}));
    EXPECT_EQ(layout.root(), b);
    EXPECT_EQ(root_node(layout).panels, keys({"b", "a"}));
    EXPECT_TRUE(layout.is_visible("a"));
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, MoveOntoItselfIsNoChange) {
    DockLayout layout = side_by_side();
    const DockLayout before = layout;
    const DockNodeId a = stack_of(layout, "a");
    for (const DockZone zone: {DockZone::Center, DockZone::Left, DockZone::Right, DockZone::Top, DockZone::Bottom}) {
        EXPECT_FALSE(layout.move("a", {a, zone}));
    }
    EXPECT_FALSE(layout.move("missing", {a, DockZone::Center}));
    EXPECT_FALSE(layout.move("a", {999, DockZone::Center}));
    EXPECT_EQ(layout, before);

    DockLayout single;
    single.add("a", {});
    EXPECT_FALSE(single.move("a", {kNoDockNode, DockZone::Left}));
    EXPECT_FALSE(single.move("a", {kNoDockNode, DockZone::Center}));
}

TEST(DockLayout, MoveToAnEdgeOfItsOwnStackSplitsOffTheTab) {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {});
    const DockNodeId stack = layout.root();
    EXPECT_FALSE(layout.move("b", {stack, DockZone::Center}));
    ASSERT_TRUE(layout.move("b", {stack, DockZone::Right}));
    const DockNode &root = root_node(layout);
    EXPECT_EQ(root.first, stack);
    EXPECT_EQ(layout.node(root.second)->panels, keys({"b"}));
    EXPECT_EQ(layout.node(stack)->panels, keys({"a"}));
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, MoveToASplitThatCollapsesRetargetsItsSurvivor) {
    // (a | b) over c; moving a to the right of the (a | b) split leaves b there, so a splits b.
    DockLayout layout;
    layout.add("a", {});
    layout.add("c", {stack_of(layout, "a"), DockZone::Bottom});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    const DockNodeId top = root_node(layout).first;
    ASSERT_EQ(layout.node(top)->kind, DockNodeKind::Split);
    ASSERT_TRUE(layout.move("a", {top, DockZone::Right}));
    EXPECT_EQ(layout.node(top), nullptr);
    const DockNode &root = root_node(layout);
    const DockNode &upper = *layout.node(root.first);
    EXPECT_EQ(upper.kind, DockNodeKind::Split);
    EXPECT_EQ(layout.node(upper.first)->panels, keys({"b"}));
    EXPECT_EQ(layout.node(upper.second)->panels, keys({"a"}));
    EXPECT_EQ(layout.node(root.second)->panels, keys({"c"}));
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, FloatPanelMakesAFloatOnTop) {
    DockLayout layout = side_by_side();
    const Rect rect{50.0f, 60.0f, 300.0f, 200.0f};
    ASSERT_TRUE(layout.float_panel("a", rect));
    ASSERT_EQ(layout.floats().size(), 1u);
    const DockFloat &f = layout.floats().back();
    EXPECT_EQ(f.rect, rect);
    EXPECT_EQ(layout.node(f.root)->panels, keys({"a"}));
    EXPECT_EQ(layout.find("a")->float_id, f.id);
    EXPECT_EQ(layout.float_of(f.root), f.id);
    EXPECT_EQ(layout.root(), stack_of(layout, "b"));
    EXPECT_TRUE(layout.is_visible("a"));
    EXPECT_TRUE(layout.valid());

    // A panel alone in its float only gets the new rect and is raised.
    const DockNodeId a_float = f.id;
    ASSERT_TRUE(layout.float_panel("b", rect));
    const Rect moved{0.0f, 0.0f, 100.0f, 100.0f};
    ASSERT_TRUE(layout.float_panel("a", moved));
    EXPECT_EQ(layout.floats().size(), 2u);
    EXPECT_EQ(layout.floats().back().id, a_float);
    EXPECT_EQ(layout.floats().back().rect, moved);
    EXPECT_EQ(layout.root(), kNoDockNode);
    EXPECT_FALSE(layout.float_panel("missing", rect));
}

TEST(DockLayout, FloatsHoldTabsAndSplitsAndEmptyFloatsGoAway) {
    DockLayout layout;
    layout.add("a", {});
    layout.float_panel("a", {0.0f, 0.0f, 200.0f, 200.0f});
    const DockNodeId float_id = layout.floats().front().id;
    const DockNodeId a = stack_of(layout, "a");
    ASSERT_TRUE(layout.add("b", {a, DockZone::Center}));
    ASSERT_TRUE(layout.add("c", {a, DockZone::Bottom}));
    EXPECT_EQ(layout.find("b")->float_id, float_id);
    EXPECT_EQ(layout.find("c")->float_id, float_id);
    EXPECT_EQ(layout.node(layout.floats().front().root)->kind, DockNodeKind::Split);

    ASSERT_TRUE(layout.move("c", {kNoDockNode, DockZone::Center}));
    EXPECT_EQ(layout.root(), stack_of(layout, "c"));
    EXPECT_EQ(layout.floats().front().root, a);
    layout.remove("a");
    layout.remove("b");
    EXPECT_TRUE(layout.floats().empty());
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, DockFloatAppendsTabsOrInsertsItsSubtree) {
    DockLayout layout = side_by_side();
    layout.add("c", {});  // fails: root is a split
    layout.add("c", {stack_of(layout, "b"), DockZone::Center});
    layout.float_panel("c", {0.0f, 0.0f, 100.0f, 100.0f});
    layout.add("d", {stack_of(layout, "c"), DockZone::Center});
    layout.activate("c");
    const DockNodeId tabs_float = layout.floats().back().id;

    ASSERT_TRUE(layout.dock_float(tabs_float, {stack_of(layout, "a"), DockZone::Center}));
    EXPECT_TRUE(layout.floats().empty());
    EXPECT_EQ(layout.node(stack_of(layout, "a"))->panels, keys({"a", "c", "d"}));
    EXPECT_TRUE(layout.is_visible("c"));

    layout.float_panel("c", {0.0f, 0.0f, 100.0f, 100.0f});
    layout.add("d2", {stack_of(layout, "c"), DockZone::Right});
    const DockNodeId split_float = layout.floats().back().id;
    const DockNodeId split_root = layout.floats().back().root;
    const DockLayout before = layout;
    EXPECT_FALSE(layout.dock_float(split_float, {stack_of(layout, "a"), DockZone::Center}));
    EXPECT_FALSE(layout.dock_float(split_float, {stack_of(layout, "c"), DockZone::Left}));  // inside itself
    EXPECT_FALSE(layout.dock_float(999, {stack_of(layout, "a"), DockZone::Left}));
    EXPECT_EQ(layout, before);

    ASSERT_TRUE(layout.dock_float(split_float, {kNoDockNode, DockZone::Top}));
    EXPECT_EQ(root_node(layout).first, split_root);
    EXPECT_EQ(layout.node(split_root)->parent, layout.root());
    EXPECT_TRUE(layout.floats().empty());
    EXPECT_TRUE(layout.valid());
}

TEST(DockLayout, RaiseAndRectKeepZOrderLastOnTop) {
    DockLayout layout;
    for (const char *key: {"a", "b", "c"}) {
        layout.add(key, {});
        layout.float_panel(key, {0.0f, 0.0f, 100.0f, 100.0f});
    }
    const std::vector<DockFloat> order(layout.floats().begin(), layout.floats().end());
    ASSERT_TRUE(layout.raise_float(order[0].id));
    EXPECT_EQ(layout.floats()[0].id, order[1].id);
    EXPECT_EQ(layout.floats()[1].id, order[2].id);
    EXPECT_EQ(layout.floats()[2].id, order[0].id);
    EXPECT_FALSE(layout.raise_float(999));

    const Rect rect{10.0f, 20.0f, 30.0f, 40.0f};
    ASSERT_TRUE(layout.set_float_rect(order[1].id, rect));
    EXPECT_EQ(layout.find_float(order[1].id)->rect, rect);
    EXPECT_FALSE(layout.set_float_rect(999, rect));
}

TEST(DockLayout, ActivateAndReorderKeepTheActivePanel) {
    DockLayout layout;
    for (const char *key: {"a", "b", "c", "d"}) {
        layout.add(key, {});
    }
    ASSERT_TRUE(layout.activate("b"));
    ASSERT_TRUE(layout.reorder("d", 0));
    EXPECT_EQ(root_node(layout).panels, keys({"d", "a", "b", "c"}));
    EXPECT_TRUE(layout.is_visible("b"));
    ASSERT_TRUE(layout.reorder("b", 99));
    EXPECT_EQ(root_node(layout).panels, keys({"d", "a", "c", "b"}));
    EXPECT_TRUE(layout.is_visible("b"));
    EXPECT_FALSE(layout.activate("missing"));
    EXPECT_FALSE(layout.reorder("missing", 0));
}

TEST(DockLayout, SetRatioClamps) {
    DockLayout layout = side_by_side();
    ASSERT_TRUE(layout.set_ratio(layout.root(), 0.3f));
    EXPECT_FLOAT_EQ(root_node(layout).ratio, 0.3f);
    ASSERT_TRUE(layout.set_ratio(layout.root(), -4.0f));
    EXPECT_FLOAT_EQ(root_node(layout).ratio, engine::ui::kDockRatioMin);
    ASSERT_TRUE(layout.set_ratio(layout.root(), 4.0f));
    EXPECT_FLOAT_EQ(root_node(layout).ratio, engine::ui::kDockRatioMax);
    EXPECT_FALSE(layout.set_ratio(layout.root(), std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(layout.set_ratio(stack_of(layout, "a"), 0.5f));
    EXPECT_FALSE(layout.set_ratio(999, 0.5f));
}

TEST(DockLayout, NodeIdsStayStableAndAreNeverReused) {
    DockLayout layout = side_by_side();
    const DockNodeId a = stack_of(layout, "a");
    const DockNodeId b = stack_of(layout, "b");
    layout.add("c", {b, DockZone::Bottom});
    EXPECT_EQ(stack_of(layout, "a"), a);
    EXPECT_EQ(stack_of(layout, "b"), b);
    const DockNodeId c = stack_of(layout, "c");
    layout.remove("c");
    layout.add("c", {b, DockZone::Bottom});
    EXPECT_NE(stack_of(layout, "c"), c);
    EXPECT_GT(layout.next_id(), stack_of(layout, "c"));
}

TEST(DockLayout, FromPartsRejectsBrokenLayouts) {
    const auto tabs = [](DockNodeId id, std::vector<std::string> panels, std::size_t active = 0) {
        DockNode n;
        n.id = id;
        n.panels = std::move(panels);
        n.active = active;
        return n;
    };
    const auto split = [](DockNodeId id, DockNodeId first, DockNodeId second, float ratio = 0.5f) {
        DockNode n;
        n.id = id;
        n.kind = DockNodeKind::Split;
        n.first = first;
        n.second = second;
        n.ratio = ratio;
        return n;
    };
    const auto parts = [](std::vector<DockNode> list) {
        std::map<DockNodeId, DockNode> nodes;
        for (DockNode &n: list) {
            nodes.emplace(n.id, n);
        }
        return nodes;
    };
    DockNode child_a = tabs(1, {"a"});
    child_a.parent = 3;
    DockNode child_b = tabs(2, {"b"});
    child_b.parent = 3;
    EXPECT_TRUE(DockLayout::from_parts(parts({child_a, child_b, split(3, 1, 2)}), 3, {}, 4).has_value());

    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {})}), 1, {}, 2));                       // empty stack
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"}, 1)}), 1, {}, 2));                // active
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a", "a"})}), 1, {}, 2));              // duplicate key
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {""})}), 1, {}, 2));                    // empty key
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"})}), 1, {}, 1));                   // next_id
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"}), tabs(2, {"b"})}), 1, {}, 3));   // orphan
    EXPECT_FALSE(DockLayout::from_parts(parts({child_a, split(3, 1, 0)}), 3, {}, 4));           // one child
    EXPECT_FALSE(DockLayout::from_parts(parts({child_a, child_b, split(3, 1, 2, 1.0f)}), 3, {}, 4));  // ratio
    EXPECT_FALSE(DockLayout::from_parts(parts({child_a, child_b, split(3, 1, 1)}), 3, {}, 4));  // shared child
    EXPECT_FALSE(DockLayout::from_parts(parts({child_a, child_b, split(3, 1, 2)}), 1, {}, 4));  // bad parent
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"})}), 2, {}, 3));                   // unknown root
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"}), tabs(2, {"a"})}), 1,
                                        {DockFloat{3, 2, {}}}, 4));                             // key twice
    EXPECT_FALSE(DockLayout::from_parts(parts({tabs(1, {"a"})}), kNoDockNode, {DockFloat{1, 1, {}}}, 2));  // float id
    EXPECT_FALSE(DockLayout::from_parts({}, kNoDockNode, {DockFloat{1, kNoDockNode, {}}}, 2));  // empty float
    EXPECT_TRUE(DockLayout::from_parts({}, kNoDockNode, {}, 1).has_value());
}

TEST(DockLayout, RandomOperationsKeepEveryInvariant) {
    std::mt19937 rng(188);
    const std::vector<std::string> pool{"p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7"};
    const auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
    DockLayout layout;
    DockMetrics metrics;
    for (int step = 0; step < 4000; ++step) {
        const std::string &key = pool[pick(pool.size())];
        std::vector<DockNodeId> ids{kNoDockNode, 999};
        for (const auto &[id, n]: layout.nodes()) {
            ids.push_back(id);
        }
        const DockTarget target{ids[pick(ids.size())], static_cast<DockZone>(pick(5))};
        const DockNodeId float_id = layout.floats().empty() ? 999 : layout.floats()[pick(layout.floats().size())].id;
        const DockLayout before = layout;
        bool changed = false;
        switch (pick(10)) {
            case 0:
            case 1:
                changed = layout.add(key, target);
                break;
            case 2:
                changed = layout.remove(key);
                break;
            case 3:
            case 4:
                changed = layout.move(key, target);
                break;
            case 5:
                changed = layout.float_panel(key, {static_cast<float>(pick(500)), 10.0f, 200.0f, 150.0f});
                break;
            case 6:
                changed = layout.dock_float(float_id, target);
                break;
            case 7:
                changed = layout.raise_float(float_id);
                break;
            case 8:
                changed = layout.reorder(key, pick(4));
                break;
            default:
                changed = layout.set_ratio(target.node, static_cast<float>(pick(120)) / 100.0f - 0.1f);
                break;
        }
        ASSERT_TRUE(layout.valid()) << "step " << step;
        if (!changed) {
            ASSERT_EQ(layout, before) << "step " << step;
        }
        const std::vector<std::string> panels = layout.panels();
        const DockGeometry geometry = compute_dock_geometry(layout, kArea, metrics);
        ASSERT_EQ(geometry.panels.size(), panels.size());
        ASSERT_EQ(engine::ui::dock_layout_from_text(engine::ui::dock_layout_to_text(layout)), layout);
    }
}

// --- Geometry --------------------------------------------------------------------------------------------------

TEST(DockGeometry, OneStackFillsTheAreaUnderItsTabStrip) {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {});
    const DockGeometry g = compute_dock_geometry(layout, kArea, DockMetrics{});
    ASSERT_EQ(g.stacks.size(), 1u);
    const auto &s = g.stacks.front();
    EXPECT_EQ(s.rect, kArea);
    EXPECT_EQ(s.strip, (Rect{0.0f, 0.0f, 800.0f, 24.0f}));
    EXPECT_EQ(s.content, (Rect{0.0f, 24.0f, 800.0f, 576.0f}));
    ASSERT_EQ(s.tabs.size(), 2u);
    EXPECT_EQ(s.tabs[0].rect, (Rect{0.0f, 0.0f, 120.0f, 24.0f}));
    EXPECT_EQ(s.tabs[1].rect, (Rect{120.0f, 0.0f, 120.0f, 24.0f}));
    EXPECT_FALSE(s.tabs[0].active);
    EXPECT_TRUE(s.tabs[1].active);
    ASSERT_NE(g.panel("a"), nullptr);
    EXPECT_FALSE(g.panel("a")->shown);
    EXPECT_TRUE(g.panel("b")->shown);
    EXPECT_EQ(g.panel("b")->content, s.content);
    EXPECT_EQ(g.panel("missing"), nullptr);
}

TEST(DockGeometry, MeasuredTabsShrinkToFitTheStrip) {
    DockLayout layout;
    for (const char *key: {"a", "bb", "ccc"}) {
        layout.add(key, {});
    }
    DockMetrics metrics;
    metrics.tab_width_for = [](std::string_view key) { return 20.0f * static_cast<float>(key.size()); };
    DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const auto &tabs = g.stacks.front().tabs;
    EXPECT_EQ(tabs[0].rect, (Rect{0.0f, 0.0f, 20.0f, 24.0f}));
    EXPECT_EQ(tabs[1].rect, (Rect{20.0f, 0.0f, 40.0f, 24.0f}));
    EXPECT_EQ(tabs[2].rect, (Rect{60.0f, 0.0f, 60.0f, 24.0f}));

    g = compute_dock_geometry(layout, {0.0f, 0.0f, 60.0f, 100.0f}, metrics);
    const auto &narrow = g.stacks.front().tabs;
    EXPECT_FLOAT_EQ(narrow[0].rect.w, 10.0f);
    EXPECT_FLOAT_EQ(narrow[1].rect.x, 10.0f);
    EXPECT_FLOAT_EQ(narrow[2].rect.x + narrow[2].rect.w, 60.0f);
}

TEST(DockGeometry, SplitsPlaceTheSplitterAndRespectMinimumSizes) {
    DockLayout layout = side_by_side();
    layout.set_ratio(layout.root(), 0.25f);
    const DockMetrics metrics;
    DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    ASSERT_EQ(g.splitters.size(), 1u);
    EXPECT_EQ(g.splitters[0].grab, (Rect{199.0f, 0.0f, 4.0f, 600.0f}));
    EXPECT_EQ(g.splitters[0].area, kArea);
    EXPECT_EQ(g.stack(stack_of(layout, "a"))->rect, (Rect{0.0f, 0.0f, 199.0f, 600.0f}));
    EXPECT_EQ(g.stack(stack_of(layout, "b"))->rect, (Rect{203.0f, 0.0f, 597.0f, 600.0f}));

    layout.set_ratio(layout.root(), 0.01f);
    g = compute_dock_geometry(layout, kArea, metrics);
    EXPECT_EQ(g.stack(stack_of(layout, "a"))->rect, (Rect{0.0f, 0.0f, 48.0f, 600.0f}));

    // No room for both minimums: the ratio holds.
    g = compute_dock_geometry(layout, {0.0f, 0.0f, 54.0f, 100.0f}, metrics);
    EXPECT_FLOAT_EQ(g.stack(stack_of(layout, "a"))->rect.w, 0.5f);

    DockLayout vertical;
    vertical.add("a", {});
    vertical.add("b", {vertical.root(), DockZone::Bottom});
    g = compute_dock_geometry(vertical, kArea, metrics);
    EXPECT_EQ(g.splitters[0].grab, (Rect{0.0f, 298.0f, 800.0f, 4.0f}));
    EXPECT_EQ(g.stack(stack_of(vertical, "b"))->content, (Rect{0.0f, 326.0f, 800.0f, 274.0f}));
}

TEST(DockGeometry, FloatsHaveAFrameTitleAndContent) {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {});
    layout.float_panel("b", {100.0f, 100.0f, 300.0f, 200.0f});
    const DockGeometry g = compute_dock_geometry(layout, kArea, DockMetrics{});
    ASSERT_EQ(g.floats.size(), 1u);
    const auto &f = g.floats[0];
    EXPECT_EQ(f.frame, (Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    EXPECT_EQ(f.title, (Rect{104.0f, 104.0f, 292.0f, 22.0f}));
    EXPECT_EQ(f.content, (Rect{104.0f, 126.0f, 292.0f, 170.0f}));
    const auto *b = g.panel("b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->float_id, f.id);
    EXPECT_TRUE(b->shown);
    EXPECT_EQ(b->content, (Rect{104.0f, 150.0f, 292.0f, 146.0f}));
    EXPECT_EQ(g.stack(b->stack)->float_id, f.id);
    EXPECT_EQ(g.panel("a")->content, (Rect{0.0f, 24.0f, 800.0f, 576.0f}));
    EXPECT_EQ(g.floating(f.id), &f);
}

// --- Drop ------------------------------------------------------------------------------------------------------

TEST(DockDrop, EmptyRootTakesTheWholeAreaAndOutsideFloats) {
    DockLayout layout;
    layout.add("a", {});
    layout.float_panel("a", {600.0f, 400.0f, 100.0f, 100.0f});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    // The float holds only `a`, so it is skipped and the empty dock area is under the pointer.
    const auto in = engine::ui::dock_drop_for_panel(layout, g, metrics, {650.0f, 450.0f}, "a", {10.0f, 5.0f});
    ASSERT_TRUE(in.has_value());
    EXPECT_EQ(*in, (DockDrop{DockDropKind::Dock, {kNoDockNode, DockZone::Center}, kArea}));

    const auto out = engine::ui::dock_drop_for_panel(layout, g, metrics, {900.0f, 50.0f}, "a", {10.0f, 5.0f});
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->kind, DockDropKind::Float);
    EXPECT_EQ(out->preview, (Rect{890.0f, 45.0f, 320.0f, 240.0f}));
    EXPECT_FALSE(engine::ui::dock_drop_for_panel(layout, g, metrics, {10.0f, 10.0f}, "missing", {}));
}

TEST(DockDrop, StackZonesAreCentreAndFourEdgeBands) {
    DockLayout layout = side_by_side();
    layout.add("c", {stack_of(layout, "a"), DockZone::Center});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const DockNodeId b = stack_of(layout, "b");
    const Rect brect = g.stack(b)->rect;  // {402, 0, 398, 600}
    ASSERT_EQ(brect, (Rect{402.0f, 0.0f, 398.0f, 600.0f}));
    const auto drop = [&](float x, float y) {
        return engine::ui::dock_drop_for_panel(layout, g, metrics, {x, y}, "c", {});
    };
    EXPECT_EQ(drop(600.0f, 300.0f), (DockDrop{DockDropKind::Dock, {b, DockZone::Center}, brect}));
    EXPECT_EQ(drop(600.0f, 10.0f)->target, (DockTarget{b, DockZone::Center}));  // tab strip, inside the root band
    EXPECT_EQ(drop(420.0f, 300.0f),
              (DockDrop{DockDropKind::Dock, {b, DockZone::Left}, Rect{402.0f, 0.0f, 197.0f, 600.0f}}));
    EXPECT_EQ(drop(760.0f, 300.0f),
              (DockDrop{DockDropKind::Dock, {b, DockZone::Right}, Rect{603.0f, 0.0f, 197.0f, 600.0f}}));
    EXPECT_EQ(drop(600.0f, 100.0f),
              (DockDrop{DockDropKind::Dock, {b, DockZone::Top}, Rect{402.0f, 0.0f, 398.0f, 298.0f}}));
    EXPECT_EQ(drop(600.0f, 560.0f),
              (DockDrop{DockDropKind::Dock, {b, DockZone::Bottom}, Rect{402.0f, 302.0f, 398.0f, 298.0f}}));
    EXPECT_FALSE(drop(400.0f, 300.0f).has_value());  // splitter

    const auto applied = drop(760.0f, 300.0f);
    ASSERT_TRUE(layout.move("c", applied->target));
    const DockGeometry after = compute_dock_geometry(layout, kArea, metrics);
    EXPECT_EQ(after.stack(stack_of(layout, "c"))->rect, applied->preview);
}

TEST(DockDrop, RootEdgeBandsDockToTheWholeArea) {
    DockLayout layout = side_by_side();
    layout.add("c", {stack_of(layout, "a"), DockZone::Center});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const auto drop = [&](float x, float y) {
        return engine::ui::dock_drop_for_panel(layout, g, metrics, {x, y}, "c", {});
    };
    EXPECT_EQ(drop(5.0f, 300.0f),
              (DockDrop{DockDropKind::Dock, {kNoDockNode, DockZone::Left}, Rect{0.0f, 0.0f, 398.0f, 600.0f}}));
    EXPECT_EQ(drop(300.0f, 590.0f),
              (DockDrop{DockDropKind::Dock, {kNoDockNode, DockZone::Bottom}, Rect{0.0f, 302.0f, 800.0f, 298.0f}}));
    // Just inside the band the stack's own band applies.
    EXPECT_EQ(drop(30.0f, 300.0f)->target, (DockTarget{stack_of(layout, "a"), DockZone::Left}));
}

TEST(DockDrop, DroppingOnItselfIsNothing) {
    DockLayout layout = side_by_side();
    const DockMetrics metrics;
    DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    for (const glm::vec2 p: {glm::vec2{200.0f, 300.0f}, glm::vec2{50.0f, 300.0f}, glm::vec2{200.0f, 560.0f}}) {
        EXPECT_FALSE(engine::ui::dock_drop_for_panel(layout, g, metrics, p, "a", {}).has_value());
    }
    layout.add("c", {stack_of(layout, "a"), DockZone::Center});
    g = compute_dock_geometry(layout, kArea, metrics);
    EXPECT_FALSE(engine::ui::dock_drop_for_panel(layout, g, metrics, {200.0f, 300.0f}, "c", {}).has_value());
    EXPECT_EQ(engine::ui::dock_drop_for_panel(layout, g, metrics, {200.0f, 560.0f}, "c", {})->target,
              (DockTarget{stack_of(layout, "a"), DockZone::Bottom}));

    DockLayout single;
    single.add("a", {});
    g = compute_dock_geometry(single, kArea, metrics);
    EXPECT_FALSE(engine::ui::dock_drop_for_panel(single, g, metrics, {5.0f, 300.0f}, "a", {}).has_value());
}

TEST(DockDrop, FloatsAboveTheDockCatchTheDrop) {
    DockLayout layout = side_by_side();
    layout.add("f1", {stack_of(layout, "a"), DockZone::Center});
    layout.float_panel("f1", {100.0f, 100.0f, 300.0f, 200.0f});
    layout.add("f2", {stack_of(layout, "f1"), DockZone::Center});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const DockNodeId fstack = stack_of(layout, "f1");
    // Content of the float: its stack, not the docked stack below.
    EXPECT_EQ(engine::ui::dock_drop_for_panel(layout, g, metrics, {250.0f, 200.0f}, "b", {})->target,
              (DockTarget{fstack, DockZone::Center}));
    // Title bar: a float frame with no stack under the point.
    EXPECT_FALSE(engine::ui::dock_drop_for_panel(layout, g, metrics, {250.0f, 110.0f}, "b", {}).has_value());

    // Dragging the float as a whole skips it.
    const DockNodeId float_id = layout.floats().front().id;
    EXPECT_EQ(engine::ui::dock_drop_for_float(layout, g, metrics, {250.0f, 200.0f}, float_id)->target,
              (DockTarget{stack_of(layout, "a"), DockZone::Center}));
    EXPECT_EQ(engine::ui::dock_drop_for_float(layout, g, metrics, {795.0f, 300.0f}, float_id)->target,
              (DockTarget{kNoDockNode, DockZone::Right}));
    EXPECT_FALSE(engine::ui::dock_drop_for_float(layout, g, metrics, {900.0f, 300.0f}, float_id).has_value());
    EXPECT_FALSE(engine::ui::dock_drop_for_float(layout, g, metrics, {250.0f, 200.0f}, 999).has_value());

    const auto drop = engine::ui::dock_drop_for_float(layout, g, metrics, {600.0f, 300.0f}, float_id);
    ASSERT_TRUE(layout.dock_float(float_id, drop->target));
    EXPECT_EQ(layout.node(stack_of(layout, "b"))->panels, keys({"b", "f1", "f2"}));

    // A float whose root is a split cannot join a stack at Center.
    layout.float_panel("f1", {100.0f, 100.0f, 300.0f, 200.0f});
    layout.move("f2", {stack_of(layout, "f1"), DockZone::Right});
    const DockGeometry g2 = compute_dock_geometry(layout, kArea, metrics);
    const DockNodeId split_float = layout.floats().front().id;
    EXPECT_FALSE(engine::ui::dock_drop_for_float(layout, g2, metrics, {600.0f, 300.0f}, split_float).has_value());
    EXPECT_EQ(engine::ui::dock_drop_for_float(layout, g2, metrics, {600.0f, 560.0f}, split_float)->target,
              (DockTarget{stack_of(layout, "b"), DockZone::Bottom}));
}

// --- Chrome ----------------------------------------------------------------------------------------------------

TEST(DockChrome, TabsSplittersTitlesAndEdges) {
    DockLayout layout = side_by_side();
    layout.add("c", {stack_of(layout, "b"), DockZone::Center});
    layout.add("f", {stack_of(layout, "a"), DockZone::Center});
    layout.float_panel("f", {100.0f, 100.0f, 300.0f, 200.0f});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const DockNodeId float_id = layout.floats().front().id;
    const auto at = [&](float x, float y) { return engine::ui::dock_chrome_at(g, metrics, {x, y}); };

    EXPECT_EQ(at(410.0f, 10.0f), (DockChromeHit{DockChromeKind::Tab, stack_of(layout, "b"), "b", {}}));
    EXPECT_EQ(at(530.0f, 10.0f), (DockChromeHit{DockChromeKind::Tab, stack_of(layout, "b"), "c", {}}));
    EXPECT_FALSE(at(700.0f, 10.0f).has_value());  // strip past the last tab
    EXPECT_EQ(at(401.0f, 300.0f), (DockChromeHit{DockChromeKind::Splitter, layout.root(), {}, {}}));
    EXPECT_FALSE(at(600.0f, 300.0f).has_value());  // panel content

    EXPECT_EQ(at(110.0f, 130.0f), (DockChromeHit{DockChromeKind::Tab, stack_of(layout, "f"), "f", {}}));
    EXPECT_EQ(at(300.0f, 110.0f), (DockChromeHit{DockChromeKind::FloatTitle, float_id, {}, {}}));
    EXPECT_EQ(at(101.0f, 200.0f),
              (DockChromeHit{DockChromeKind::FloatEdge, float_id, {}, DockResizeEdges{true, false, false, false}}));
    EXPECT_EQ(at(398.0f, 298.0f),
              (DockChromeHit{DockChromeKind::FloatEdge, float_id, {}, DockResizeEdges{false, true, false, true}}));
    // Float content is not chrome.
    EXPECT_FALSE(at(250.0f, 250.0f).has_value());
    EXPECT_FALSE(at(900.0f, 10.0f).has_value());

    EXPECT_EQ(engine::ui::dock_float_at(g, {250.0f, 250.0f}), float_id);
    EXPECT_EQ(engine::ui::dock_float_at(g, {50.0f, 50.0f}), kNoDockNode);
}

TEST(DockChrome, AFloatHidesTheTabsUnderIt) {
    DockLayout layout;
    layout.add("a", {});
    layout.add("f", {});
    layout.float_panel("f", {0.0f, 0.0f, 300.0f, 200.0f});
    layout.add("g", {});
    layout.float_panel("g", {50.0f, 50.0f, 100.0f, 100.0f});
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    // The docked tab "a" at {0,0} is under the first float: its title bar wins.
    EXPECT_EQ(engine::ui::dock_chrome_at(g, metrics, {10.0f, 10.0f})->kind, DockChromeKind::FloatTitle);
    // Second float on top of the first.
    EXPECT_EQ(engine::ui::dock_float_at(g, {60.0f, 60.0f}), layout.floats().back().id);
    EXPECT_TRUE(layout.raise_float(layout.floats().front().id));
    const DockGeometry raised = compute_dock_geometry(layout, kArea, metrics);
    EXPECT_EQ(engine::ui::dock_float_at(raised, {60.0f, 60.0f}), layout.floats().back().id);
    EXPECT_EQ(layout.floats().back().root, stack_of(layout, "f"));
}

TEST(DockChrome, SplitterDragClampsToMinimumSizes) {
    DockLayout layout = side_by_side();
    const DockMetrics metrics;
    const DockGeometry g = compute_dock_geometry(layout, kArea, metrics);
    const auto &s = g.splitters.front();
    EXPECT_FLOAT_EQ(engine::ui::dock_split_ratio_at(s, {200.0f, 0.0f}, metrics), 198.0f / 796.0f);
    EXPECT_FLOAT_EQ(engine::ui::dock_split_ratio_at(s, {-50.0f, 0.0f}, metrics), 48.0f / 796.0f);
    EXPECT_FLOAT_EQ(engine::ui::dock_split_ratio_at(s, {900.0f, 0.0f}, metrics), 748.0f / 796.0f);
    ASSERT_TRUE(layout.set_ratio(s.split, engine::ui::dock_split_ratio_at(s, {200.0f, 0.0f}, metrics)));
    const DockGeometry moved = compute_dock_geometry(layout, kArea, metrics);
    EXPECT_FLOAT_EQ(moved.splitters.front().grab.x + 2.0f, 200.0f);

    engine::ui::DockSplitterRect tiny = s;
    tiny.area = {0.0f, 0.0f, 60.0f, 100.0f};
    EXPECT_FLOAT_EQ(engine::ui::dock_split_ratio_at(tiny, {10.0f, 0.0f}, metrics), 0.5f);

    engine::ui::DockSplitterRect vertical = s;
    vertical.axis = DockAxis::Vertical;
    EXPECT_FLOAT_EQ(engine::ui::dock_split_ratio_at(vertical, {0.0f, 300.0f}, metrics), 0.5f);
}

TEST(DockChrome, FloatResizeAndClamp) {
    const DockMetrics metrics;  // min content 48, border 4, title 22: min frame 56 x 78
    const Rect start{100.0f, 100.0f, 300.0f, 200.0f};
    EXPECT_EQ(engine::ui::dock_float_resized(start, {false, true, false, true}, {20.0f, 30.0f}, metrics),
              (Rect{100.0f, 100.0f, 320.0f, 230.0f}));
    EXPECT_EQ(engine::ui::dock_float_resized(start, {true, false, true, false}, {20.0f, 30.0f}, metrics),
              (Rect{120.0f, 130.0f, 280.0f, 170.0f}));
    EXPECT_EQ(engine::ui::dock_float_resized(start, {true, false, false, false}, {1000.0f, 0.0f}, metrics),
              (Rect{344.0f, 100.0f, 56.0f, 200.0f}));
    EXPECT_EQ(engine::ui::dock_float_resized(start, {false, false, false, true}, {0.0f, -1000.0f}, metrics),
              (Rect{100.0f, 100.0f, 300.0f, 78.0f}));

    EXPECT_EQ(engine::ui::dock_float_clamped({700.0f, -20.0f, 300.0f, 200.0f}, kArea),
              (Rect{500.0f, 0.0f, 300.0f, 200.0f}));
    EXPECT_EQ(engine::ui::dock_float_clamped({50.0f, 50.0f, 900.0f, 200.0f}, kArea),
              (Rect{0.0f, 50.0f, 900.0f, 200.0f}));
}

// --- Text ------------------------------------------------------------------------------------------------------

TEST(DockText, RoundTripsExactly) {
    DockLayout layout = side_by_side();
    layout.set_ratio(layout.root(), 1.0f / 3.0f);
    layout.add("quote \"x\" \\ back", {stack_of(layout, "a"), DockZone::Center});
    layout.add("line\nbreak\ttab", {stack_of(layout, "b"), DockZone::Bottom});
    layout.set_ratio(layout.node(stack_of(layout, "b"))->parent, 0.3f);
    layout.add("\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB5\xD0\xBA\xD1\x82", {stack_of(layout, "b"), DockZone::Center});
    layout.activate("a");
    layout.add("f", {stack_of(layout, "a"), DockZone::Center});
    layout.float_panel("f", {-12.5f, 0.1f, 333.333f, 1e7f});
    layout.add("g", {stack_of(layout, "f"), DockZone::Top});
    layout.add("h", {});  // fails: root is a split; nothing changes
    ASSERT_TRUE(layout.valid());

    const std::string text = engine::ui::dock_layout_to_text(layout);
    const std::optional<DockLayout> back = engine::ui::dock_layout_from_text(text);
    ASSERT_TRUE(back.has_value()) << text;
    EXPECT_EQ(*back, layout);
    EXPECT_EQ(engine::ui::dock_layout_to_text(*back), text);

    const auto empty = engine::ui::dock_layout_from_text(engine::ui::dock_layout_to_text(DockLayout{}));
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(*empty, DockLayout{});
}

TEST(DockText, RandomFloatsRoundTripBitForBit) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> any(-1.0e6f, 1.0e6f);
    std::uniform_real_distribution<float> share(engine::ui::kDockRatioMin, engine::ui::kDockRatioMax);
    DockLayout layout = side_by_side();
    layout.float_panel("b", {});
    for (int i = 0; i < 2000; ++i) {
        layout.set_float_rect(layout.floats().front().id, {any(rng), any(rng), any(rng), share(rng)});
        const auto back = engine::ui::dock_layout_from_text(engine::ui::dock_layout_to_text(layout));
        ASSERT_TRUE(back.has_value());
        ASSERT_EQ(*back, layout) << engine::ui::dock_layout_to_text(layout);
    }
    DockLayout split = side_by_side();
    for (int i = 0; i < 2000; ++i) {
        split.set_ratio(split.root(), share(rng));
        const auto back = engine::ui::dock_layout_from_text(engine::ui::dock_layout_to_text(split));
        ASSERT_TRUE(back.has_value());
        ASSERT_EQ(*back, split);
    }
}

TEST(DockText, GarbageIsNullopt) {
    const std::string good = engine::ui::dock_layout_to_text(side_by_side());
    ASSERT_TRUE(engine::ui::dock_layout_from_text(good).has_value());
    const auto replaced = [&](std::string_view from, std::string_view to) {
        std::string text = good;
        const std::size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        return text.replace(at, from.size(), to);
    };
    for (const std::string &text: {
                 std::string{},
                 std::string{"not = toml = at all ["},
                 std::string{"\x01\x02\x03"},
                 replaced("version = 1", "version = 2"),
                 replaced("version = 1", "version = \"1\""),
                 replaced("root = ", "rot = "),
                 replaced("kind = \"tabs\"", "kind = \"grid\""),
                 replaced("active = 0", "active = 5"),
                 replaced("active = 0", "active = -1"),
                 replaced("panels = [\"a\"]", "panels = [1]"),
                 replaced("panels = [\"a\"]", "panels = []"),
                 replaced("panels = [\"a\"]", "panels = [\"b\"]"),
                 replaced("axis = \"horizontal\"", "axis = \"diagonal\""),
                 replaced("ratio = 0.5", "ratio = 2.0"),
                 replaced("ratio = 0.5", "ratio = \"half\""),
                 replaced("id = 1", "id = 2"),
                 replaced("id = 1", "id = -1"),
                 replaced("next_id = 4", "next_id = 2"),
                 good + "\n[[float]]\nid = 9\nroot = 1\nrect = [0.0, 0.0, 1.0]\n",
                 good + "\n[[float]]\nid = 9\nroot = 1\nrect = [0.0, 0.0, 1.0, 1.0]\n",
                 "node = 3\n" + good,
         }) {
        EXPECT_FALSE(engine::ui::dock_layout_from_text(text).has_value()) << text;
    }
}

// --- Reconcile -------------------------------------------------------------------------------------------------

TEST(DockReconcile, DropsUnknownAndAddsMissingPanels) {
    DockLayout layout = side_by_side();
    layout.add("old", {stack_of(layout, "a"), DockZone::Center});
    const std::vector<std::string> registered{"a", "b", "new"};
    EXPECT_TRUE(engine::ui::reconcile_dock_layout(layout, registered, DockSpot{"b", DockZone::Center}));
    EXPECT_FALSE(layout.contains("old"));
    EXPECT_EQ(layout.node(stack_of(layout, "b"))->panels, keys({"b", "new"}));
    EXPECT_TRUE(layout.valid());
    EXPECT_FALSE(engine::ui::reconcile_dock_layout(layout, registered, DockSpot{"b", DockZone::Center}));

    // `beside` missing: the zone applies to the dock area.
    const std::vector<std::string> more{"a", "b", "new", "side"};
    EXPECT_TRUE(engine::ui::reconcile_dock_layout(layout, more, DockSpot{"gone", DockZone::Left}));
    EXPECT_EQ(root_node(layout).first, stack_of(layout, "side"));

    // Center on a split root falls back to the first docked stack.
    const std::vector<std::string> last{"a", "b", "new", "side", "extra"};
    EXPECT_TRUE(engine::ui::reconcile_dock_layout(layout, last, DockSpot{}));
    EXPECT_EQ(stack_of(layout, "extra"), stack_of(layout, "side"));

    DockLayout empty;
    EXPECT_TRUE(engine::ui::reconcile_dock_layout(empty, registered, DockSpot{"x", DockZone::Center}));
    EXPECT_EQ(root_node(empty).panels, keys({"a", "b", "new"}));
}
