#include "element_path.h"
#include "painter.h"

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
        constexpr float kInspectorWidth = 360.0f;

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

        std::string element_label(const Element &element, bool selected, int depth) {
            std::string label(static_cast<std::size_t>(std::max(depth, 0) * 2), ' ');
            if (selected) {
                label += "* ";
            }
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
                world->ctx<UiInspector>().selection[window] = std::move(pick);
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
            row.label.set(element_label(element, selected, depth));
            if (!row.has_children) {
                row.twist.set(" ");
            } else if (row.expanded) {
                row.twist.set("-");
            } else {
                row.twist.set("+");
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
            std::string text = element_label(element, false, 0);
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

        constexpr std::string_view kInspectorCss = R"(
#inspector { background: #1b1b1bf2; }
#root { width: 100%; padding: 8px; }
#title { font-size: 16px; margin: 6px; color: #f2f2f2; }
#tree { height: 200px; background: #111111; }
#detail_scroll { height: 160px; }
#detail { font-size: 12px; color: #dddddd; }
#rules { height: 140px; }
.twist { width: 28px; height: 22px; font-size: 12px; color: #f0f0f0; background: #2a2a2a; }
.row { height: 22px; font-size: 12px; color: #f0f0f0; background: #2a2a2a; }
.rule { font-size: 12px; color: #dddddd; }
)";

        std::expected<UiDocument, UiError> build_inspector_document() {
            auto twist = button().with_class("twist").command_bind(intern("toggle")).content_bind(intern("twist"));
            auto row = button().with_class("row").command_bind(intern("select")).content_bind(intern("label"));
            auto row_stack = stack().direction(StackDirection::Horizontal);
            row_stack.add(std::move(twist));
            row_stack.add(std::move(row));

            auto row_template = item_template();
            row_template.add(std::move(row_stack));
            auto rows = items_control().items_source_bind(intern("rows"));
            rows.add(std::move(row_template));
            auto tree = scroll_view().with_id("tree").overflow_y(Overflow::Scroll);
            tree.add(std::move(rows));

            auto detail = label().with_id("detail").text_bind(intern("detail"));
            auto detail_scroll = scroll_view().with_id("detail_scroll").overflow_y(Overflow::Scroll);
            detail_scroll.add(std::move(detail));

            auto rule = label().with_class("rule").text_bind(intern("line"));
            auto rule_template = item_template();
            rule_template.add(std::move(rule));
            auto rule_list = items_control().items_source_bind(intern("rules"));
            rule_list.add(std::move(rule_template));
            auto rules = scroll_view().with_id("rules").overflow_y(Overflow::Scroll);
            rules.add(std::move(rule_list));

            auto root_stack = stack().with_id("root").direction(StackDirection::Vertical);
            root_stack.add(label().with_id("title").text("UI Inspector"));
            root_stack.add(std::move(tree));
            root_stack.add(std::move(detail_scroll));
            root_stack.add(std::move(rules));

            auto root = canvas().with_id("inspector");
            root.add(std::move(root_stack));
            return make_document(std::move(root));
        }

        std::optional<Stylesheet> inspector_stylesheet() {
            std::vector<std::string> warnings;
            auto sheet = parse_css(kInspectorCss, warnings);
            if (!sheet) {
                log::error("ui inspector stylesheet failed to parse");
                return std::nullopt;
            }
            return std::move(*sheet);
        }

        render::Rect inspector_panel_rect(ecs::World &world, WindowId window) {
            const WindowSize size = window_size_for(world, window);
            const float width = static_cast<float>(size.width);
            const float height = static_cast<float>(size.height);
            const float panel_w = std::min(kInspectorWidth, std::max(0.0f, width));
            return render::Rect{std::max(0.0f, width - panel_w), 0.0f, panel_w, height};
        }

        std::vector<WindowId> live_windows(ecs::World &world) {
            std::unordered_set<WindowId> ids;
            ids.insert(kPrimaryWindow);
            for (const auto &[id, size]: world.ctx<WindowSizes>().sizes) {
                (void) size;
                ids.insert(id);
            }
            {
                auto view = world.view<UiCanvas>();
                for (ecs::Entity entity: view) {
                    ids.insert(view.get<UiCanvas>(entity).window);
                }
            }
            return std::vector<WindowId>(ids.begin(), ids.end());
        }

        std::optional<ecs::Entity> panel_for(ecs::World &world, WindowId window) {
            std::optional<ecs::Entity> found;
            auto view = world.view<InspectorPanel>();
            for (ecs::Entity entity: view) {
                if (view.get<InspectorPanel>(entity).window == window) {
                    found = entity;
                }
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
            const render::Rect rect = inspector_panel_rect(world, window);
            if (const std::optional<ecs::Entity> existing = panel_for(world, window)) {
                if (UiCanvas *canvas = world.try_get<UiCanvas>(*existing)) {
                    canvas->rect = rect;
                    canvas->order = kInspectorOrder;
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
            canvas.fit = UiFit::Fixed;
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
            int order = 0;
            std::uint32_t index = 0;
        };

        void fill_panel(ecs::World &world, ecs::Entity panel_entity, WindowId window) {
            UiCanvas *canvas = world.try_get<UiCanvas>(panel_entity);
            UiInstance *instance = world.try_get<UiInstance>(panel_entity);
            if (canvas == nullptr || instance == nullptr || !canvas->data_context) {
                return;
            }
            auto *model = dynamic_cast<InspectorModel *>(canvas->data_context.get());
            if (model == nullptr) {
                return;
            }

            std::vector<CanvasSource> sources;
            {
                auto view = world.view<UiCanvas>();
                for (ecs::Entity entity: view) {
                    if (world.try_get<InspectorPanel>(entity) != nullptr) {
                        continue;
                    }
                    const UiCanvas &source = view.get<UiCanvas>(entity);
                    if (source.window != window || world.try_get<UiInstance>(entity) == nullptr) {
                        continue;
                    }
                    sources.push_back(CanvasSource{entity, source.order, entity.index});
                }
            }
            std::stable_sort(sources.begin(), sources.end(), [](const CanvasSource &a, const CanvasSource &b) {
                if (a.order != b.order) {
                    return a.order < b.order;
                }
                return a.index < b.index;
            });

            const InspectorPick *pick = nullptr;
            const auto pick_it = world.ctx<UiInspector>().selection.find(window);
            if (pick_it != world.ctx<UiInspector>().selection.end()) {
                pick = &pick_it->second;
            }

            std::vector<std::shared_ptr<InspectorRow>> visible;
            std::unordered_set<RowKey, RowKeyHash> seen;
            WalkState state;
            state.world = &world;
            state.window = window;
            state.pick = pick;
            state.model = model;
            state.visible = &visible;
            state.seen = &seen;
            for (const CanvasSource &source: sources) {
                UiInstance *source_instance = world.try_get<UiInstance>(source.entity);
                if (source_instance == nullptr) {
                    continue;
                }
                state.canvas = source.entity;
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

            const WindowSize size = window_size_for(world, window);
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

    } // namespace

    void set_inspector_enabled(ecs::World &world, bool enabled) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!enabled) {
            inspector.enabled = false;
            destroy_panels(world);
            inspector.selection.clear();
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
        for (const WindowId window: live_windows(world)) {
            ensure_panel(world, window);
        }
    }

    void sync_inspector_content(ecs::World &world) {
        if (!inspector_enabled(world)) {
            return;
        }
        retarget_selection(world);
        std::vector<std::pair<ecs::Entity, WindowId>> panels;
        {
            auto view = world.view<InspectorPanel>();
            for (ecs::Entity entity: view) {
                panels.emplace_back(entity, view.get<InspectorPanel>(entity).window);
            }
        }
        for (const auto &[entity, window]: panels) {
            fill_panel(world, entity, window);
        }
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
