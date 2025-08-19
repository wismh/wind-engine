#include "ui/dock_chrome.h"

#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>

#include <string_view>
#include <utility>

namespace engine::ui {

    namespace {

        constexpr BindingId kX = intern("x");
        constexpr BindingId kY = intern("y");
        constexpr BindingId kW = intern("w");
        constexpr BindingId kH = intern("h");
        constexpr BindingId kTitle = intern("title");
        constexpr BindingId kActive = intern("active");
        constexpr BindingId kClose = intern("close");
        constexpr BindingId kCloseX = intern("cx");
        constexpr BindingId kCloseY = intern("cy");
        constexpr BindingId kCloseSize = intern("cs");
        constexpr BindingId kReserve = intern("reserve");

        constexpr BindingId kFrames = intern("frames");
        constexpr BindingId kTitles = intern("titles");
        constexpr BindingId kStacks = intern("stacks");
        constexpr BindingId kStrips = intern("strips");
        constexpr BindingId kSplitters = intern("splitters");
        constexpr BindingId kTabs = intern("tabs");
        constexpr BindingId kPreviews = intern("previews");

        Node placed(Node node, std::string_view classes) {
            node.with_class("dock-box").with_class(classes).var("x", kX).var("y", kY).var("w", kW).var("h", kH);
            return node;
        }

        Node list(std::string_view classes, BindingId items, Node row) {
            Node control = items_control();
            control.with_class(classes).items_source_bind(items);
            Node templ = item_template();
            templ.add(std::move(row));
            control.add(std::move(templ));
            return control;
        }

        Node tab_row() {
            Node row = placed(stack(), "dock-tab-item");
            Node tab = button();
            tab.with_class("dock-tab").content_bind(kTitle).checked_bind(kActive).var("reserve", kReserve);
            Node close = button();
            close.with_class("dock-tab-close")
                    .content("\xC3\x97")
                    .var("close", kClose)
                    .var("cx", kCloseX)
                    .var("cy", kCloseY)
                    .var("cs", kCloseSize);
            row.add(std::move(tab));
            row.add(std::move(close));
            return row;
        }

        void fill_list(BindableList<std::shared_ptr<DockChromeItem>> &list, const std::vector<DockChromeBox> &boxes,
                       glm::vec2 origin) {
            std::vector<std::shared_ptr<DockChromeItem>> &items = list.get();
            while (items.size() > boxes.size()) {
                items.pop_back();
            }
            while (items.size() < boxes.size()) {
                items.push_back(std::make_shared<DockChromeItem>());
            }
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                const DockChromeBox &box = boxes[i];
                DockChromeItem &item = *items[i];
                item.x.set(box.rect.x - origin.x);
                item.y.set(box.rect.y - origin.y);
                item.w.set(box.rect.w);
                item.h.set(box.rect.h);
                item.title.set(box.title);
                item.active.set(box.active);
                const bool has_close = box.close.w > 0.0f && box.close.h > 0.0f;
                item.close.set(has_close ? "block" : "none");
                item.cx.set(has_close ? box.close.x - box.rect.x : 0.0f);
                item.cy.set(has_close ? box.close.y - box.rect.y : 0.0f);
                item.cs.set(has_close ? box.close.w : 0.0f);
                item.reserve.set(has_close ? box.rect.x + box.rect.w - box.close.x : 0.0f);
            }
        }

    } // namespace

    DockChromeItem::DockChromeItem() {
        property(kX, x);
        property(kY, y);
        property(kW, w);
        property(kH, h);
        property(kTitle, title);
        property(kActive, active);
        property(kClose, close);
        property(kCloseX, cx);
        property(kCloseY, cy);
        property(kCloseSize, cs);
        property(kReserve, reserve);
    }

    DockChromeViewModel::DockChromeViewModel() {
        property(kFrames, frames);
        property(kTitles, titles);
        property(kStacks, stacks);
        property(kStrips, strips);
        property(kSplitters, splitters);
        property(kTabs, tabs);
        property(kPreviews, previews);
    }

    UiDocument build_dock_chrome_document() {
        Node root = canvas();
        root.with_class("dock-chrome");
        root.add(list("dock-frames", kFrames, placed(stack(), "dock-frame")));
        Node title = placed(button(), "dock-title");
        title.content_bind(kTitle);
        root.add(list("dock-titles", kTitles, std::move(title)));
        root.add(list("dock-stacks", kStacks, placed(stack(), "dock-stack")));
        root.add(list("dock-strips", kStrips, placed(stack(), "dock-strip")));
        root.add(list("dock-splitters", kSplitters, placed(button(), "dock-splitter")));
        root.add(list("dock-tabs", kTabs, tab_row()));
        root.add(list("dock-previews", kPreviews, placed(stack(), "dock-preview")));
        // The builder takes no {tr} here and every binding is an interned id, so the tree always builds.
        return *make_document(std::move(root));
    }

    UiDocument build_dock_tab_probe() {
        Node root = canvas();
        root.with_class("dock-chrome");
        root.add(list("dock-tabs", kTabs, tab_row()));
        UiDocument probe = *make_document(std::move(root));
        // The template's row, as the ItemsControl would generate it.
        Element &control = probe.root.children.front();
        control.generated_items.push_back(control.children.front().children.front());
        return probe;
    }

    Element &dock_tab_probe_button(UiDocument &probe) {
        return probe.root.children.front().generated_items.front().children.front();
    }

    void fill_dock_chrome(DockChromeViewModel &vm, const DockChromeContent &content, glm::vec2 origin) {
        fill_list(vm.frames, content.frames, origin);
        fill_list(vm.titles, content.titles, origin);
        fill_list(vm.stacks, content.stacks, origin);
        fill_list(vm.strips, content.strips, origin);
        fill_list(vm.splitters, content.splitters, origin);
        fill_list(vm.tabs, content.tabs, origin);
        fill_list(vm.previews, content.previews, origin);
    }

} // namespace engine::ui
