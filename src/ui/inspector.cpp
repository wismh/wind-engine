#include "element_path.h"
#include "painter.h"

#include <engine/ecs/events.h>
#include <engine/log.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/inspector.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ui {
    namespace {

        constexpr int kInspectorOrder = 10000;
        constexpr int kInspectorWindowWidth = 420;
        constexpr int kInspectorWindowHeight = 640;
        // Headless tests have no InspectorWindowHost. This id is never handed to the window manager
        // (real secondary ids start at 1 and climb). The panel canvas and WindowSizes use it.
        constexpr WindowId kHeadlessInspectorWindow{0xFFFFFFF0u};

        struct InspectorCloseCursor {
            ecs::EventCursor<WindowCloseRequestedEvent> cursor;
        };

        const char *kind_name(ElementKind kind) {
            switch (kind) {
                case ElementKind::Canvas:
                    return "Canvas";
                case ElementKind::Stack:
                    return "Stack";
                case ElementKind::Label:
                    return "Label";
                case ElementKind::Button:
                    return "Button";
                case ElementKind::Image:
                    return "Image";
                case ElementKind::ItemsControl:
                    return "ItemsControl";
                case ElementKind::ItemTemplate:
                    return "ItemTemplate";
                case ElementKind::Line:
                    return "Line";
                case ElementKind::Component:
                    return "Component";
                case ElementKind::Viewport:
                    return "Viewport";
                case ElementKind::TextInput:
                    return "TextInput";
                case ElementKind::ScrollView:
                    return "ScrollView";
                case ElementKind::Checkbox:
                    return "Checkbox";
                case ElementKind::Math:
                    return "Math";
            }
            return "Element";
        }

        const char *position_name(PositionMode mode) {
            switch (mode) {
                case PositionMode::Static:
                    return "static";
                case PositionMode::Relative:
                    return "relative";
                case PositionMode::Absolute:
                    return "absolute";
            }
            return "static";
        }

        const char *overflow_name(Overflow overflow) {
            switch (overflow) {
                case Overflow::Visible:
                    return "visible";
                case Overflow::Hidden:
                    return "hidden";
                case Overflow::Scroll:
                    return "scroll";
                case Overflow::Auto:
                    return "auto";
            }
            return "visible";
        }

        const char *align_name(UiAlign align) {
            switch (align) {
                case UiAlign::Start:
                    return "start";
                case UiAlign::Center:
                    return "center";
                case UiAlign::End:
                    return "end";
                case UiAlign::SpaceBetween:
                    return "space-between";
            }
            return "start";
        }

        std::string format_length(const Length &length) {
            if (!length.calc.empty()) {
                return "calc(...)";
            }
            const char *unit = "px";
            if (length.unit == LengthUnit::Percent) {
                unit = "%";
            } else if (length.unit == LengthUnit::Em) {
                unit = "em";
            }
            return std::format("{:.4g}{}", length.value, unit);
        }

        std::string format_insets(const LengthInsets &insets) {
            return format_length(insets.top) + " " + format_length(insets.right) + " " + format_length(insets.bottom) +
                   " " + format_length(insets.left);
        }

        std::string format_color(glm::vec4 color) {
            return std::format("{:.2f},{:.2f},{:.2f},{:.2f}", color.x, color.y, color.z, color.w);
        }

        std::string format_rect(const render::Rect &rect) {
            return std::format("{:.1f} {:.1f} {:.1f} {:.1f}", rect.x, rect.y, rect.w, rect.h);
        }

        const char *motion_prop_name(MotionProp prop) {
            switch (prop) {
                case MotionProp::Color:
                    return "color";
                case MotionProp::Background:
                    return "background";
                case MotionProp::Opacity:
                    return "opacity";
                case MotionProp::Visibility:
                    return "visibility";
                case MotionProp::Display:
                    return "display";
                case MotionProp::Width:
                    return "width";
                case MotionProp::Height:
                    return "height";
                case MotionProp::Padding:
                    return "padding";
                case MotionProp::Margin:
                    return "margin";
                case MotionProp::Transform:
                    return "transform";
                default:
                    return "property";
            }
        }

        std::string element_label(const Element &element, int depth) {
            std::string label(static_cast<std::size_t>(std::max(depth, 0) * 2), ' ');
            label += kind_name(element.kind);
            if (!element.id.empty()) {
                label += " #";
                label += element.id;
            }
            for (const std::string &class_name: element.classes) {
                label += " .";
                label += class_name;
            }
            if (element.is_virtualization_spacer) {
                label += " spacer";
            }
            if (element.display_none) {
                label += " display:none";
            } else if (!element.visible) {
                label += " hidden";
            }
            return label;
        }

        void append_binding(std::string &out, const char *name, BindingId id) {
            if (!is_bound(id)) {
                return;
            }
            out += name;
            out += ": bound\n";
        }

        bool collect_ancestors(const Element &current, const Element &target, std::vector<const Element *> &ancestors) {
            if (&current == &target) {
                return true;
            }
            ancestors.push_back(&current);
            for (const Element &child: current.children) {
                if (collect_ancestors(child, target, ancestors)) {
                    return true;
                }
            }
            for (const Element &child: current.generated_items) {
                if (collect_ancestors(child, target, ancestors)) {
                    return true;
                }
            }
            ancestors.pop_back();
            return false;
        }

        class InspectorRuleRow final : public ViewModel {
        public:
            Bindable<std::string> line;

            InspectorRuleRow() { property(intern("line"), line); }
        };

        class InspectorRow final : public ViewModel {
        public:
            Bindable<std::string> label;
            Bindable<std::string> twist;
            Bindable<std::string> row_fill;
            RelayCommand select;
            RelayCommand toggle;
            bool expanded = true;
            bool has_children = false;
            ecs::World *world = nullptr;
            WindowId window = kPrimaryWindow;
            ecs::Entity canvas{};
            std::vector<std::size_t> path;
            const void *owner = nullptr;

            InspectorRow() {
                property(intern("label"), label);
                property(intern("twist"), twist);
                property(intern("row"), row_fill);
                command(intern("select"), select);
                command(intern("toggle"), toggle);
                select = [this] { choose(); };
                toggle = [this] {
                    if (has_children) {
                        expanded = !expanded;
                    }
                };
            }

            void choose() {
                if (world == nullptr) {
                    return;
                }
                InspectorPick pick;
                pick.canvas = canvas;
                pick.path = path;
                pick.generated_owner = owner;
                pick.active = true;
                UiInspector &inspector = world->ctx<UiInspector>();
                inspector.detail_window = window;
                inspector.selection[window] = std::move(pick);
            }
        };

        struct RowKey {
            std::uint32_t canvas_index = 0;
            std::uint32_t canvas_generation = 0;
            const void *owner = nullptr;
            std::vector<std::size_t> relative;

            bool operator==(const RowKey &other) const {
                return canvas_index == other.canvas_index && canvas_generation == other.canvas_generation &&
                       owner == other.owner && relative == other.relative;
            }
        };

        struct RowKeyHash {
            std::size_t operator()(const RowKey &key) const {
                std::size_t hash = key.canvas_index;
                hash = hash * 131u + key.canvas_generation;
                hash ^= reinterpret_cast<std::uintptr_t>(key.owner);
                for (const std::size_t step: key.relative) {
                    hash = hash * 131u + step;
                }
                return hash;
            }
        };

        class InspectorModel final : public ViewModel {
        public:
            Bindable<std::string> detail;
            BindableList<std::shared_ptr<InspectorRow>> rows;
            BindableList<std::shared_ptr<InspectorRuleRow>> rules;
            std::unordered_map<RowKey, std::shared_ptr<InspectorRow>, RowKeyHash> cache;

            InspectorModel() {
                property(intern("detail"), detail);
                property(intern("rows"), rows);
                property(intern("rules"), rules);
                detail.set("Nothing selected");
            }
        };

        struct WalkState {
            ecs::World *world = nullptr;
            WindowId window = kPrimaryWindow;
            ecs::Entity canvas{};
            const InspectorPick *pick = nullptr;
            bool show_window = false;
            InspectorModel *model = nullptr;
            std::vector<std::shared_ptr<InspectorRow>> *visible = nullptr;
            std::unordered_set<RowKey, RowKeyHash> *seen = nullptr;
        };

        void walk_element(Element &element, const WalkState &state, std::vector<std::size_t> path,
                          std::vector<std::size_t> relative, const void *owner, int depth) {
            RowKey key;
            key.canvas_index = state.canvas.index;
            key.canvas_generation = state.canvas.generation;
            key.owner = owner;
            key.relative = relative;

            std::shared_ptr<InspectorRow> &slot = state.model->cache[key];
            if (!slot) {
                slot = std::make_shared<InspectorRow>();
            }
            InspectorRow &row = *slot;
            row.world = state.world;
            row.window = state.window;
            row.canvas = state.canvas;
            row.path = path;
            row.owner = owner;
            row.has_children = !element.children.empty() || !element.generated_items.empty();
            const bool selected = state.pick != nullptr && state.pick->active && state.pick->canvas == state.canvas &&
                                  state.pick->path == path;
            std::string label = element_label(element, depth);
            if (depth == 0 && state.show_window) {
                label.insert(0, std::format("[{}] ", static_cast<std::uint32_t>(state.window)));
            }
            row.label.set(std::move(label));
            row.row_fill.set(selected ? "#1c4634" : "#00000000");
            if (!row.has_children) {
                row.twist.set(" ");
            } else if (row.expanded) {
                row.twist.set("▾");
            } else {
                row.twist.set("▸");
            }
            state.seen->insert(key);
            state.visible->push_back(slot);

            if (row.has_children && !row.expanded) {
                return;
            }
            for (std::size_t i = 0; i < element.children.size(); ++i) {
                std::vector<std::size_t> child_path = path;
                child_path.push_back(i);
                std::vector<std::size_t> child_relative = relative;
                child_relative.push_back(i);
                walk_element(element.children[i], state, std::move(child_path), std::move(child_relative), owner,
                             depth + 1);
            }
            for (std::size_t i = 0; i < element.generated_items.size(); ++i) {
                Element &item = element.generated_items[i];
                std::vector<std::size_t> child_path = path;
                child_path.push_back(i | kGeneratedPathBit);
                const void *child_owner = owner;
                std::vector<std::size_t> child_relative = relative;
                if (item.generated_owner != nullptr) {
                    child_owner = item.generated_owner;
                    child_relative.clear();
                } else {
                    child_relative.push_back(i | kGeneratedPathBit);
                }
                walk_element(item, state, std::move(child_path), std::move(child_relative), child_owner, depth + 1);
            }
        }

        std::string describe_element(Element &root, Element &element, const Stylesheet *sheet, float window_width,
                                     float window_height) {
            std::string text = element_label(element, 0);
            text += "\n";
            if (!element.text.empty()) {
                constexpr std::size_t kMax = 80;
                text += "text: ";
                text += element.text.substr(0, std::min(element.text.size(), kMax));
                if (element.text.size() > kMax) {
                    text += "...";
                }
                text += "\n";
            }
            text += "pseudo:";
            bool any_pseudo = false;
            if (element.hovered) {
                text += " hover";
                any_pseudo = true;
            }
            if (element.pressed) {
                text += " pressed";
                any_pseudo = true;
            }
            if (element.disabled) {
                text += " disabled";
                any_pseudo = true;
            }
            if (element.focused) {
                text += " focus";
                any_pseudo = true;
            }
            if (element.checked) {
                text += " checked";
                any_pseudo = true;
            }
            if (!any_pseudo) {
                text += " (none)";
            }
            text += "\n";

            const LayoutBoxes boxes = layout_boxes(root, element);
            text += "border: " + format_rect(boxes.border) + "\n";
            text += "margin box: " + format_rect(boxes.margin) + "\n";
            text += "content: " + format_rect(boxes.content) + "\n";

            if (element.style_cache_paint_.valid) {
                const ComputedStyle &style = element.style_cache_paint_.style;
                text += "color: " + format_color(style.color) + "\n";
                text += "background: " + format_color(style.background) + "\n";
                text += std::format("opacity: {:.2f}\n", style.opacity);
                text += "font-size: " + format_length(style.font_size) + "\n";
                if (style.width) {
                    text += "width: " + format_length(*style.width) + "\n";
                }
                if (style.height) {
                    text += "height: " + format_length(*style.height) + "\n";
                }
                text += "padding: " + format_insets(style.padding) + "\n";
                text += "margin: " + format_insets(style.margin) + "\n";
                text += std::format("z-index: {}\n", style.z_index);
                text += "position: ";
                text += position_name(style.position);
                text += "\n";
                text += "text-align: ";
                text += align_name(style.text_align);
                text += "\n";
                text += "overflow: ";
                text += overflow_name(style.overflow_x);
                text += " ";
                text += overflow_name(style.overflow_y);
                text += "\n";
                text += style.display_none ? "display: none\n" : "display: (shown)\n";
                text += style.visible ? "visibility: visible\n" : "visibility: hidden\n";
                text += "border-width: " + format_length(style.border_width) + "\n";
                text += "border-color: " + format_color(style.border_color) + "\n";
            } else {
                text += "computed: not painted yet\n";
            }

            for (const ShownMotion &shown: element.motion_shown) {
                if (!shown.running) {
                    continue;
                }
                text += motion_prop_name(shown.prop);
                text += " ";
                if (shown.prop == MotionProp::Color || shown.prop == MotionProp::Background ||
                    shown.prop == MotionProp::BorderColor || shown.prop == MotionProp::Stroke) {
                    text += format_color(shown.value.color);
                } else if (shown.prop == MotionProp::Visibility || shown.prop == MotionProp::Display) {
                    text += shown.value.flag ? "true" : "false";
                } else {
                    const float number = shown.value.px != 0.0f ? shown.value.px : shown.value.number;
                    text += std::format("{:.2f}", number);
                }
                text += " (animated)\n";
            }

            append_binding(text, "text", element.text_binding);
            append_binding(text, "content", element.content_binding);
            append_binding(text, "command", element.command_binding);
            append_binding(text, "checked", element.checked_binding);
            append_binding(text, "source", element.source_binding);
            append_binding(text, "items", element.items_source_binding);
            append_binding(text, "drag", element.drag_binding);
            append_binding(text, "paint", element.paint_binding);
            append_binding(text, "pan-x", element.pan_x_binding);
            append_binding(text, "pan-y", element.pan_y_binding);
            append_binding(text, "zoom", element.zoom_binding);
            append_binding(text, "scroll-x", element.scroll_x_binding);
            append_binding(text, "scroll-y", element.scroll_y_binding);

            std::vector<const Element *> ancestors;
            collect_ancestors(root, element, ancestors);
            const std::vector<MatchedRule> rules =
                    match_style_rules(element, sheet, ancestors, window_width, window_height);
            text += std::format("rules: {}\n", rules.size());
            return text;
        }

        std::vector<std::shared_ptr<InspectorRuleRow>> rule_rows_for(Element &root, Element &element,
                                                                     const Stylesheet *sheet, float window_width,
                                                                     float window_height) {
            std::vector<const Element *> ancestors;
            collect_ancestors(root, element, ancestors);
            const std::vector<MatchedRule> matched =
                    match_style_rules(element, sheet, ancestors, window_width, window_height);
            std::vector<std::shared_ptr<InspectorRuleRow>> rows;
            rows.reserve(matched.size());
            for (std::size_t i = 0; i < matched.size(); ++i) {
                const MatchedRule &rule = matched[i];
                auto row = std::make_shared<InspectorRuleRow>();
                std::string line = rule.selector;
                line += std::format(" ({})", rule.specificity);
                if (i + 1 == matched.size()) {
                    line += " winner";
                }
                for (const CssDeclaration &decl: rule.declarations) {
                    line += "\n  ";
                    line += decl.property;
                    line += ": ";
                    line += decl.value;
                }
                row->line.set(std::move(line));
                rows.push_back(std::move(row));
            }
            return rows;
        }

        // Chrome inside #root, which has gap 6 (six gaps = 36): title 22, three .section at 14,
        // detail 176, rules 152. That is 392. The tree is the rest of the window, at least 96px.
        constexpr std::string_view kInspectorCss = R"(
#inspector { background: #121418; }
#root { width: 100%; height: 100%; padding: 12px; gap: 6px; }
#title { height: 22px; font-size: 15px; color: #f3f5f8; font-family: default; }
.section { height: 14px; font-size: 11px; color: #8b93a3; font-family: default; }
#tree {
    width: 100%;
    height: calc(100% - 428px);
    min-height: 96px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
    padding: 4px;
    overflow-y: auto;
    scrollbar-width: thin;
    scrollbar-color: #3a4150 #1a1d24;
}
#detail_scroll {
    width: 100%;
    height: 176px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
    overflow-y: auto;
    scrollbar-width: thin;
    scrollbar-color: #3a4150 #1a1d24;
}
#detail { font-size: 12px; color: #d5dbe4; font-family: default; padding: 10px; }
#rules {
    width: 100%;
    height: 152px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
    padding: 4px 0;
    overflow-y: auto;
    scrollbar-width: thin;
    scrollbar-color: #3a4150 #1a1d24;
}
.rule { font-size: 12px; color: #c5ced8; font-family: default; padding: 2px 10px; }
.rows { width: 100%; }
.rules { width: 100%; }
.rowline { width: 100%; height: 24px; background: var(--row, #00000000); border-radius: 4px; }
.twist {
    width: 20px;
    height: 24px;
    font-size: 12px;
    color: #9aa3b2;
    font-family: default;
    background: #00000000;
    padding: 0;
    margin: 0;
    text-align: center;
    align-items: center;
}
.row {
    width: calc(100% - 20px);
    height: 24px;
    font-size: 13px;
    color: #e6ebf2;
    font-family: default;
    background: #00000000;
    padding: 0 8px 0 2px;
    margin: 0;
    white-space: nowrap;
    align-items: center;
}
.twist:hover { color: #ffffff; background: #ffffff14; }
.row:hover { color: #ffffff; background: #ffffff14; }
)";

        std::expected<UiDocument, UiError> build_inspector_document() {
            auto twist = button().with_class("twist").command_bind(intern("toggle")).content_bind(intern("twist"));
            auto row = button().with_class("row").command_bind(intern("select")).content_bind(intern("label"));
            auto row_stack =
                    stack().with_class("rowline").direction(StackDirection::Horizontal).var("row", intern("row"));
            row_stack.add(std::move(twist));
            row_stack.add(std::move(row));

            auto row_template = item_template();
            row_template.add(std::move(row_stack));
            auto rows = items_control().with_class("rows").items_source_bind(intern("rows"));
            rows.add(std::move(row_template));
            auto tree = scroll_view().with_id("tree").overflow_y(Overflow::Scroll);
            tree.add(std::move(rows));

            auto detail = label().with_id("detail").text_bind(intern("detail"));
            auto detail_scroll = scroll_view().with_id("detail_scroll").overflow_y(Overflow::Scroll);
            detail_scroll.add(std::move(detail));

            auto rule = label().with_class("rule").text_bind(intern("line"));
            auto rule_template = item_template();
            rule_template.add(std::move(rule));
            auto rule_list = items_control().with_class("rules").items_source_bind(intern("rules"));
            rule_list.add(std::move(rule_template));
            auto rules = scroll_view().with_id("rules").overflow_y(Overflow::Scroll);
            rules.add(std::move(rule_list));

            auto root_stack = stack().with_id("root").direction(StackDirection::Vertical);
            root_stack.add(label().with_id("title").text("UI Inspector"));
            root_stack.add(label().with_class("section").text("Tree"));
            root_stack.add(std::move(tree));
            root_stack.add(label().with_class("section").text("Computed"));
            root_stack.add(std::move(detail_scroll));
            root_stack.add(label().with_class("section").text("Rules"));
            root_stack.add(std::move(rules));

            auto root = canvas().with_id("inspector");
            root.add(std::move(root_stack));
            return make_document(std::move(root));
        }

        std::optional<Stylesheet> inspector_stylesheet() {
            std::vector<std::string> warnings;
            auto sheet = parse_css(kInspectorCss, warnings);
            for (const std::string &warning: warnings) {
                log::warn(std::format("ui inspector stylesheet: {}", warning));
            }
            if (!sheet) {
                log::error("ui inspector stylesheet failed to parse");
                return std::nullopt;
            }
            return std::move(*sheet);
        }

        render::Rect inspector_window_rect(ecs::World &world, WindowId window) {
            const WindowSize size = window_size_for(world, window);
            return render::Rect{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height)};
        }

        std::optional<WindowId> open_inspector_window(ecs::World &world) {
            WindowDesc desc;
            desc.title = "UI Inspector";
            desc.size = {kInspectorWindowWidth, kInspectorWindowHeight};
            InspectorWindowHost &host = world.ctx<InspectorWindowHost>();
            if (host.open) {
                return host.open(desc);
            }
            world.ctx<WindowSizes>().sizes[kHeadlessInspectorWindow] =
                    WindowSize{kInspectorWindowWidth, kInspectorWindowHeight};
            return kHeadlessInspectorWindow;
        }

        void release_inspector_window(ecs::World &world) {
            UiInspector &inspector = world.ctx<UiInspector>();
            if (!inspector.panel_window) {
                return;
            }
            const WindowId id = *inspector.panel_window;
            inspector.panel_window.reset();
            if (id == kPrimaryWindow) {
                return;
            }
            world.ctx<WindowSizes>().sizes.erase(id);
            if (id == kHeadlessInspectorWindow) {
                return;
            }
            InspectorWindowHost &host = world.ctx<InspectorWindowHost>();
            if (host.close) {
                host.close(id);
            }
        }

        std::optional<ecs::Entity> panel_entity(ecs::World &world) {
            std::optional<ecs::Entity> found;
            auto view = world.view<InspectorPanel>();
            for (ecs::Entity entity: view) {
                found = entity;
            }
            return found;
        }

        void destroy_panels(ecs::World &world) {
            std::vector<ecs::Entity> entities;
            {
                auto view = world.view<InspectorPanel>();
                for (ecs::Entity entity: view) {
                    entities.push_back(entity);
                }
            }
            for (ecs::Entity entity: entities) {
                world.destroy(entity);
            }
        }

        void ensure_panel(ecs::World &world, WindowId window) {
            const render::Rect rect = inspector_window_rect(world, window);
            if (const std::optional<ecs::Entity> existing = panel_entity(world)) {
                if (UiCanvas *canvas = world.try_get<UiCanvas>(*existing)) {
                    canvas->fit = UiFit::FillWindow;
                    canvas->rect = rect;
                    canvas->order = kInspectorOrder;
                    canvas->window = window;
                }
                return;
            }

            auto document = build_inspector_document();
            if (!document) {
                log::error("ui inspector document failed to build");
                return;
            }
            auto model = std::make_shared<InspectorModel>();
            UiCanvas canvas;
            canvas.fit = UiFit::FillWindow;
            canvas.order = kInspectorOrder;
            canvas.window = window;
            canvas.rect = rect;
            canvas.data_context = model;
            const ecs::Entity entity =
                    spawn_canvas(world, std::move(canvas), std::move(*document), inspector_stylesheet());
            world.emplace<InspectorPanel>(entity, InspectorPanel{window});
        }

        void retarget_selection(ecs::World &world) {
            UiInspector &inspector = world.ctx<UiInspector>();
            for (auto &[window, pick]: inspector.selection) {
                (void) window;
                if (!pick.active) {
                    continue;
                }
                UiInstance *instance = world.try_get<UiInstance>(pick.canvas);
                if (instance == nullptr) {
                    pick.active = false;
                    continue;
                }
                Element *element = resolve_inspector_element(instance->document.root, pick.path, pick.generated_owner);
                if (element == nullptr) {
                    continue;
                }
                pick.path = find_element_path(instance->document.root, element);
                pick.generated_owner = path_generated_owner(instance->document.root, pick.path);
            }
        }

        struct CanvasSource {
            ecs::Entity entity{};
            WindowId window = kPrimaryWindow;
            int order = 0;
            std::uint32_t index = 0;
        };

        void fill_panel(ecs::World &world, ecs::Entity panel) {
            UiCanvas *canvas = world.try_get<UiCanvas>(panel);
            UiInstance *instance = world.try_get<UiInstance>(panel);
            if (canvas == nullptr || instance == nullptr || !canvas->data_context) {
                return;
            }
            auto *model = dynamic_cast<InspectorModel *>(canvas->data_context.get());
            if (model == nullptr) {
                return;
            }

            std::vector<CanvasSource> sources;
            std::unordered_set<WindowId> source_windows;
            {
                auto view = world.view<UiCanvas>();
                for (ecs::Entity entity: view) {
                    if (world.try_get<InspectorPanel>(entity) != nullptr) {
                        continue;
                    }
                    const UiCanvas &source = view.get<UiCanvas>(entity);
                    if (world.try_get<UiInstance>(entity) == nullptr) {
                        continue;
                    }
                    sources.push_back(CanvasSource{entity, source.window, source.order, entity.index});
                    source_windows.insert(source.window);
                }
            }
            std::stable_sort(sources.begin(), sources.end(), [](const CanvasSource &a, const CanvasSource &b) {
                if (a.window != b.window) {
                    return static_cast<std::uint32_t>(a.window) < static_cast<std::uint32_t>(b.window);
                }
                if (a.order != b.order) {
                    return a.order < b.order;
                }
                return a.index < b.index;
            });

            UiInspector &inspector = world.ctx<UiInspector>();
            std::vector<std::shared_ptr<InspectorRow>> visible;
            std::unordered_set<RowKey, RowKeyHash> seen;
            WalkState state;
            state.world = &world;
            state.show_window = source_windows.size() > 1;
            state.model = model;
            state.visible = &visible;
            state.seen = &seen;
            for (const CanvasSource &source: sources) {
                UiInstance *source_instance = world.try_get<UiInstance>(source.entity);
                if (source_instance == nullptr) {
                    continue;
                }
                state.canvas = source.entity;
                state.window = source.window;
                const auto pick_it = inspector.selection.find(source.window);
                state.pick = pick_it != inspector.selection.end() ? &pick_it->second : nullptr;
                walk_element(source_instance->document.root, state, {}, {}, nullptr, 0);
            }
            for (auto it = model->cache.begin(); it != model->cache.end();) {
                if (!seen.contains(it->first)) {
                    it = model->cache.erase(it);
                } else {
                    ++it;
                }
            }
            model->rows.set(std::move(visible));

            const WindowId detail_window = inspector.detail_window;
            const auto detail_it = inspector.selection.find(detail_window);
            const InspectorPick *pick = detail_it == inspector.selection.end() ? nullptr : &detail_it->second;
            const WindowSize size = window_size_for(world, detail_window);
            if (pick == nullptr || !pick->active) {
                model->detail.set("Nothing selected");
                model->rules.set({});
                return;
            }
            UiInstance *selected_instance = world.try_get<UiInstance>(pick->canvas);
            if (selected_instance == nullptr) {
                model->detail.set("Selected element is not in the live tree.");
                model->rules.set({});
                return;
            }
            Element *selected =
                    resolve_inspector_element(selected_instance->document.root, pick->path, pick->generated_owner);
            if (selected == nullptr) {
                model->detail.set("Selected element is not in the live tree.");
                model->rules.set({});
                return;
            }
            const Stylesheet *sheet = nullptr;
            if (selected_instance->stylesheet) {
                sheet = &*selected_instance->stylesheet;
            }
            const float width = static_cast<float>(size.width);
            const float height = static_cast<float>(size.height);
            model->detail.set(describe_element(selected_instance->document.root, *selected, sheet, width, height));
            model->rules.set(rule_rows_for(selected_instance->document.root, *selected, sheet, width, height));
        }

        bool consume_inspector_close(ecs::World &world) {
            const std::optional<WindowId> panel = world.ctx<UiInspector>().panel_window;
            bool close = false;
            ecs::EventReader<WindowCloseRequestedEvent> reader(world, world.ctx<InspectorCloseCursor>().cursor);
            for (const WindowCloseRequestedEvent &event: reader) {
                if (panel && event.window == *panel) {
                    close = true;
                }
            }
            if (!close) {
                return false;
            }
            set_inspector_enabled(world, false);
            return true;
        }

    } // namespace

    void set_inspector_enabled(ecs::World &world, bool enabled) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!enabled) {
            inspector.enabled = false;
            destroy_panels(world);
            release_inspector_window(world);
            inspector.selection.clear();
            inspector.detail_window = kPrimaryWindow;
            return;
        }
        inspector.enabled = true;
        sync_inspector_frames(world);
        sync_inspector_content(world);
    }

    bool inspector_enabled(ecs::World &world) { return world.ctx<UiInspector>().enabled; }

    InspectorPick inspector_selection(ecs::World &world, WindowId window) {
        const UiInspector &inspector = world.ctx<UiInspector>();
        const auto it = inspector.selection.find(window);
        if (it == inspector.selection.end()) {
            return {};
        }
        return it->second;
    }

    void sync_inspector_frames(ecs::World &world) {
        if (!inspector_enabled(world)) {
            return;
        }
        if (consume_inspector_close(world)) {
            return;
        }
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!inspector.panel_window) {
            inspector.panel_window = open_inspector_window(world);
        }
        if (!inspector.panel_window) {
            return;
        }
        ensure_panel(world, *inspector.panel_window);
    }

    void sync_inspector_content(ecs::World &world) {
        if (!inspector_enabled(world)) {
            return;
        }
        retarget_selection(world);
        const std::optional<ecs::Entity> panel = panel_entity(world);
        if (!panel) {
            return;
        }
        fill_panel(world, *panel);
    }

    std::optional<ecs::Entity> inspector_hover_canvas(ecs::World &world, WindowId window) {
        if (!inspector_enabled(world)) {
            return std::nullopt;
        }
        const UiPointer &pointer = pointer_for(world, window);
        ecs::Entity best{};
        int best_order = 0;
        std::uint32_t best_index = 0;
        bool any = false;
        auto view = world.view<UiCanvas>();
        for (ecs::Entity entity: view) {
            const UiCanvas &canvas = view.get<UiCanvas>(entity);
            if (canvas.window != window || !rect_contains(canvas.rect, pointer.position.x, pointer.position.y)) {
                continue;
            }
            const bool higher =
                    !any || canvas.order > best_order || (canvas.order == best_order && entity.index > best_index);
            if (!higher) {
                continue;
            }
            any = true;
            best = entity;
            best_order = canvas.order;
            best_index = entity.index;
        }
        if (!any || world.try_get<InspectorPanel>(best) != nullptr) {
            return std::nullopt;
        }
        return best;
    }

} // namespace engine::ui
