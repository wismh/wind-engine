#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

// wind-227: Element::handle (a generational id in the document's arena) and Element::parent.

namespace {

    using engine::ui::Element;
    using engine::ui::ElementId;
    using engine::ui::ElementLookupError;
    using engine::ui::UiDocument;

    class RowVm final : public engine::ui::ViewModel {};

    class ListVm final : public engine::ui::ViewModel {
    public:
        engine::ui::BindableList<std::shared_ptr<RowVm>> items;

        ListVm() { property(engine::ui::intern("items"), items); }
    };

    std::vector<std::shared_ptr<RowVm>> make_rows(std::size_t count) {
        std::vector<std::shared_ptr<RowVm>> rows;
        for (std::size_t i = 0; i < count; ++i) {
            rows.push_back(std::make_shared<RowVm>());
        }
        return rows;
    }

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return sheet.value_or(engine::ui::Stylesheet{});
    }

    // The same pass canvas.cpp runs each frame.
    void run_frame(UiDocument &doc, engine::ui::ViewModel &vm, const engine::ui::Stylesheet &sheet) {
        ASSERT_TRUE(engine::ui::apply_bindings(doc, vm).has_value());
        engine::ui::apply_layout_style(doc.root, &sheet, 800.0f, 600.0f);
        engine::ui::layout(doc, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});
    }

    constexpr char kListXml[] = R"(
    <Canvas>
      <ItemsControl class="list" items_source="{binding items}">
        <ItemTemplate><Canvas class="row"><Label text="x"/></Canvas></ItemTemplate>
      </ItemsControl>
    </Canvas>
)";
    constexpr char kListCss[] = ".list { overflow-y: auto; height: 100px; gap: 2px; } .row { height: 20px; }";

    Element &items_control(UiDocument &doc) { return doc.root.children[0]; }

    // Every element, children and generated items, depth first.
    template<typename Visit>
    void walk(Element &element, Visit &&visit) {
        visit(element);
        for (Element &child: element.children) {
            walk(child, visit);
        }
        for (Element &item: element.generated_items) {
            walk(item, visit);
        }
    }

    // `generated_owner` -> element id, for the real (non-spacer) rows of the list.
    std::map<const void *, ElementId> row_ids(Element &list) {
        std::map<const void *, ElementId> out;
        for (Element &row: list.generated_items) {
            if (row.generated_owner != nullptr) {
                out[row.generated_owner] = row.handle;
            }
        }
        return out;
    }

    void scroll_to(UiDocument &doc, ListVm &vm, const engine::ui::Stylesheet &sheet, float y) {
        items_control(doc).scroll_y = y;
        run_frame(doc, vm, sheet);
        run_frame(doc, vm, sheet);
    }

    TEST(UiElementId, ParseGivesEveryElementAnIdAndItsParent) {
        auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack><Label text="a"/><Label text="b"/></Stack>
          <Button content="c"/>
        </Canvas>
    )");
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;

        std::unordered_set<ElementId> seen;
        walk(doc.root, [&](Element &element) {
            EXPECT_FALSE(element.handle.is_null());
            EXPECT_TRUE(seen.insert(element.handle).second) << "ids are unique";
            const auto found = doc.find(element.handle);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, &element);
        });
        EXPECT_TRUE(doc.root.parent.is_null());
        EXPECT_EQ(doc.root.children[0].parent, doc.root.handle);
        EXPECT_EQ(doc.root.children[0].children[1].parent, doc.root.children[0].handle);
        EXPECT_EQ(doc.root.children[1].parent, doc.root.handle);
        EXPECT_EQ(doc.arena.live_count(), seen.size());
    }

    TEST(UiElementId, TheBuilderIndexesToo) {
        engine::ui::Node root = engine::ui::canvas();
        root.add(engine::ui::stack().add(engine::ui::label()));
        auto document = engine::ui::make_document(std::move(root));
        ASSERT_TRUE(document.has_value());
        walk(document->root, [&](Element &element) { EXPECT_FALSE(element.handle.is_null()); });
        EXPECT_EQ(document->root.children[0].parent, document->root.handle);
    }

    TEST(UiElementId, AReusedRowKeepsItsIdAndANewRowGetsANewOne) {
        auto parsed = engine::ui::parse_xml(kListXml);
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        ListVm vm;
        vm.items.set(make_rows(100));
        run_frame(doc, vm, sheet);
        run_frame(doc, vm, sheet);

        const std::map<const void *, ElementId> before = row_ids(items_control(doc));
        ASSERT_GT(before.size(), 3u);
        ASSERT_LT(before.size(), 40u) << "the list is virtualized, so this tests a window that moves";

        scroll_to(doc, vm, sheet, 6.0f * 22.0f);
        const std::map<const void *, ElementId> after = row_ids(items_control(doc));

        std::size_t reused = 0;
        std::size_t created = 0;
        for (const auto &[owner, id]: after) {
            if (const auto old = before.find(owner); old != before.end()) {
                EXPECT_EQ(old->second, id) << "a row the window kept keeps its id";
                ++reused;
            } else {
                for (const auto &[other_owner, other_id]: before) {
                    EXPECT_NE(other_id, id) << "a new row never takes the id of another";
                }
                ++created;
            }
        }
        EXPECT_GT(reused, 0u);
        EXPECT_GT(created, 0u);
        for (const auto &[owner, id]: after) {
            const auto found = doc.find(id);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ((*found)->generated_owner, owner);
        }
    }

    TEST(UiElementId, ADestroyedRowsIdIsNeverGivenToALaterRow) {
        auto parsed = engine::ui::parse_xml(kListXml);
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        ListVm vm;
        vm.items.set(make_rows(200));
        run_frame(doc, vm, sheet);
        run_frame(doc, vm, sheet);

        std::set<std::pair<std::uint32_t, std::uint32_t>> all_ever;
        std::vector<ElementId> dead;
        std::map<const void *, ElementId> live = row_ids(items_control(doc));
        for (const auto &[owner, id]: live) {
            all_ever.insert({id.index, id.generation});
        }
        // Walk the window down the list and back, so slots are freed and reused.
        for (int step = 1; step <= 40; ++step) {
            const float y = (step <= 20 ? step : 40 - step) * 22.0f * 5.0f;
            scroll_to(doc, vm, sheet, y);
            const std::map<const void *, ElementId> now = row_ids(items_control(doc));
            for (const auto &[owner, id]: live) {
                const auto still = now.find(owner);
                if (still == now.end()) {
                    dead.push_back(id);
                }
            }
            for (const auto &[owner, id]: now) {
                if (live.find(owner) == live.end()) {
                    // New this step. (index, generation) was never carried by any row before.
                    EXPECT_TRUE(all_ever.insert({id.index, id.generation}).second)
                            << "the id (" << id.index << ", " << id.generation << ") was handed out before";
                }
            }
            live = now;
        }
        ASSERT_GT(dead.size(), 10u);
        for (const ElementId id: dead) {
            const auto found = doc.find(id);
            ASSERT_FALSE(found.has_value());
            EXPECT_EQ(found.error(), ElementLookupError::Stale);
        }
    }

    TEST(UiElementId, ParentIdsAreRightAfterReconcile) {
        auto parsed = engine::ui::parse_xml(kListXml);
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        ListVm vm;
        vm.items.set(make_rows(100));
        run_frame(doc, vm, sheet);
        run_frame(doc, vm, sheet);
        scroll_to(doc, vm, sheet, 30.0f * 22.0f);

        Element &list = items_control(doc);
        ASSERT_FALSE(list.generated_items.empty());
        walk(doc.root, [&](Element &element) {
            for (Element &child: element.children) {
                EXPECT_EQ(child.parent, element.handle);
            }
            for (Element &row: element.generated_items) {
                EXPECT_EQ(row.parent, element.handle);
                EXPECT_FALSE(row.handle.is_null());
                const auto found = doc.find(row.handle);
                ASSERT_TRUE(found.has_value())
                        << "row " << row.handle.index << ": " << engine::ui::element_lookup_error_name(found.error());
                EXPECT_EQ(*found, &row);
            }
        });
        // A cloned row's subtree has its own ids, not the template's.
        const Element &tmpl = list.children[0];
        for (const Element &row: list.generated_items) {
            if (row.generated_owner == nullptr) {
                continue;
            }
            EXPECT_NE(row.handle, tmpl.children[0].handle);
            ASSERT_FALSE(row.children.empty());
            EXPECT_NE(row.children[0].handle, tmpl.children[0].children[0].handle);
            EXPECT_EQ(row.children[0].parent, row.handle);
        }
    }

    TEST(UiElementId, ReindexAfterASpliceKeepsIdsAndFixesPositions) {
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="a"/><Label text="b"/><Label text="c"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        const ElementId a = doc.root.children[0].handle;
        const ElementId b = doc.root.children[1].handle;
        const ElementId c = doc.root.children[2].handle;

        // Edited by hand: a new element in front of the others.
        Element fresh;
        fresh.kind = engine::ui::ElementKind::Label;
        fresh.text = "new";
        doc.root.children.insert(doc.root.children.begin(), std::move(fresh));

        const auto before = doc.find(b);
        ASSERT_FALSE(before.has_value()) << "the arena has not been told yet";
        EXPECT_EQ(before.error(), ElementLookupError::Misplaced);

        doc.reindex();
        EXPECT_FALSE(doc.root.children[0].handle.is_null());
        EXPECT_NE(doc.root.children[0].handle, a);
        EXPECT_EQ(doc.root.children[0].parent, doc.root.handle);
        EXPECT_EQ(doc.root.children[1].handle, a);
        EXPECT_EQ(doc.root.children[2].handle, b);
        EXPECT_EQ(doc.root.children[3].handle, c);
        for (const ElementId id: {a, b, c}) {
            const auto found = doc.find(id);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ((*found)->handle, id);
        }
    }

    TEST(UiElementId, RemovingAnElementFreesItsIdAtReindex) {
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="a"/><Label text="b"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        const ElementId a = doc.root.children[0].handle;
        const ElementId b = doc.root.children[1].handle;
        doc.root.children.erase(doc.root.children.begin());
        doc.reindex();

        const auto gone = doc.find(a);
        ASSERT_FALSE(gone.has_value());
        EXPECT_EQ(gone.error(), ElementLookupError::Stale);
        const auto kept = doc.find(b);
        ASSERT_TRUE(kept.has_value());
        EXPECT_EQ((*kept)->text, "b");

        Element added;
        added.kind = engine::ui::ElementKind::Label;
        doc.root.children.push_back(std::move(added));
        doc.reindex();
        EXPECT_NE(doc.root.children[1].handle.generation, a.generation)
                << "the freed slot comes back on a new generation";
        EXPECT_FALSE(doc.find(a).has_value());
    }

    TEST(UiElementId, StaleAndNullIdsFailWithANamedError) {
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="a"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;

        const auto null = doc.find(ElementId{});
        ASSERT_FALSE(null.has_value());
        EXPECT_EQ(null.error(), ElementLookupError::Null);
        EXPECT_FALSE(engine::ui::element_lookup_error_name(null.error()).empty());

        const auto never = doc.find(ElementId{9999, 1});
        ASSERT_FALSE(never.has_value());
        EXPECT_EQ(never.error(), ElementLookupError::Stale);

        const ElementId real = doc.root.children[0].handle;
        const auto wrong_generation = doc.find(ElementId{real.index, real.generation + 1});
        ASSERT_FALSE(wrong_generation.has_value());
        EXPECT_EQ(wrong_generation.error(), ElementLookupError::Stale);

        // Children cleared without reindex: the id is alive in the arena but no longer in the tree. An error, not a
        // read past the end.
        doc.root.children.clear();
        const auto missing = doc.find(real);
        ASSERT_FALSE(missing.has_value());
        EXPECT_EQ(missing.error(), ElementLookupError::Misplaced);
        EXPECT_NE(engine::ui::element_lookup_error_name(ElementLookupError::Misplaced),
                  engine::ui::element_lookup_error_name(ElementLookupError::Stale));
    }

    TEST(UiElementId, ACopyOfTheDocumentKeepsItsIdsValid) {
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack><Label text="a"/></Stack></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        const ElementId label = parsed->root.children[0].children[0].handle;
        UiDocument copy = *parsed;
        UiDocument moved = std::move(*parsed);
        for (UiDocument *doc: {&copy, &moved}) {
            const auto found = doc->find(label);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ((*found)->text, "a");
        }
    }

    TEST(UiElementId, AnElementCopiedByHandGetsItsOwnIdAtReindex) {
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="a"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        UiDocument &doc = *parsed;
        doc.root.children.push_back(doc.root.children[0]); // the copy carries the original's id
        doc.reindex();
        EXPECT_NE(doc.root.children[0].handle, doc.root.children[1].handle);
        for (Element &child: doc.root.children) {
            const auto found = doc.find(child.handle);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, &child);
        }
    }

    TEST(UiElementId, ApplyBindingsIndexesATreeBuiltByHand) {
        UiDocument doc;
        doc.root.kind = engine::ui::ElementKind::Canvas;
        doc.root.children.emplace_back().kind = engine::ui::ElementKind::Label;
        ASSERT_TRUE(doc.root.handle.is_null());
        ListVm vm;
        ASSERT_TRUE(engine::ui::apply_bindings(doc, vm).has_value());
        EXPECT_FALSE(doc.root.handle.is_null());
        EXPECT_EQ(doc.root.children[0].parent, doc.root.handle);
    }

} // namespace
