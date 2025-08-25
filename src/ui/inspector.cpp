#include "element_path.h"
#include "painter.h"

#include <engine/ui/canvas.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ui {
    namespace {

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
                case ElementKind::Popup:
                    return "Popup";
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

        std::string element_label(const Element &element) {
            std::string label = kind_name(element.kind);
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

        std::string describe_element(Element &root, Element &element, const Stylesheet *sheet, float window_width,
                                     float window_height) {
            std::string text = element_label(element);
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
            append_binding(text, "open", element.open_binding);
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

        std::vector<std::string> rule_lines(Element &root, Element &element, const Stylesheet *sheet,
                                            float window_width, float window_height) {
            std::vector<const Element *> ancestors;
            collect_ancestors(root, element, ancestors);
            const std::vector<MatchedRule> matched =
                    match_style_rules(element, sheet, ancestors, window_width, window_height);
            std::vector<std::string> lines;
            lines.reserve(matched.size());
            for (std::size_t i = 0; i < matched.size(); ++i) {
                const MatchedRule &rule = matched[i];
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
                lines.push_back(std::move(line));
            }
            return lines;
        }

        struct CanvasSource {
            ecs::Entity entity{};
            WindowId window = kPrimaryWindow;
            int order = 0;
            std::uint32_t index = 0;
        };

        // Every canvas with a live tree, by window, then `order`, then entity index.
        std::vector<CanvasSource> live_canvases(ecs::World &world) {
            std::vector<CanvasSource> sources;
            auto view = world.view<UiCanvas>();
            for (ecs::Entity entity: view) {
                if (world.try_get<UiInstance>(entity) == nullptr) {
                    continue;
                }
                const UiCanvas &canvas = view.get<UiCanvas>(entity);
                sources.push_back(CanvasSource{entity, canvas.window, canvas.order, entity.index});
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
            return sources;
        }

        // One element on the way down the tree: where it is from the canvas root (`path`) and from its
        // nearest generated row (`relative`, `owner`), which is what its row key is made of.
        struct InspectorNode {
            const Element *element = nullptr;
            ecs::Entity canvas{};
            WindowId window = kPrimaryWindow;
            std::vector<std::size_t> path;
            std::vector<std::size_t> relative;
            const void *owner = nullptr;
        };

        struct InspectorTreeSource {
            [[nodiscard]] InspectorRowKey key(const InspectorNode &node) const {
                return InspectorRowKey{node.canvas, reinterpret_cast<std::uintptr_t>(node.owner), node.relative};
            }

            [[nodiscard]] bool has_children(const InspectorNode &node) const {
                return !node.element->children.empty() || !node.element->generated_items.empty();
            }

            template<typename Emit>
            void for_each_child(const InspectorNode &node, Emit &&emit) const {
                const Element &element = *node.element;
                for (std::size_t i = 0; i < element.children.size(); ++i) {
                    InspectorNode child{&element.children[i], node.canvas, node.window, node.path, node.relative,
                                        node.owner};
                    child.path.push_back(i);
                    child.relative.push_back(i);
                    emit(child);
                }
                for (std::size_t i = 0; i < element.generated_items.size(); ++i) {
                    const Element &item = element.generated_items[i];
                    InspectorNode child{&item, node.canvas, node.window, node.path, node.relative, node.owner};
                    child.path.push_back(i | kGeneratedPathBit);
                    if (item.generated_owner != nullptr) {
                        child.owner = item.generated_owner;
                        child.relative.clear();
                    } else {
                        child.relative.push_back(i | kGeneratedPathBit);
                    }
                    emit(child);
                }
            }
        };

        // The picked element, its canvas tree, and that canvas's window size. Null when the pick is
        // inactive or its element is gone.
        struct PickedElement {
            UiInstance *instance = nullptr;
            Element *element = nullptr;
            WindowSize size{};
        };

        PickedElement resolve_pick(ecs::World &world, const InspectorPick &pick) {
            PickedElement picked;
            if (!pick.active) {
                return picked;
            }
            UiInstance *instance = world.try_get<UiInstance>(pick.canvas);
            const UiCanvas *canvas = world.try_get<UiCanvas>(pick.canvas);
            if (instance == nullptr || canvas == nullptr) {
                return picked;
            }
            Element *element = resolve_inspector_element(instance->document.root, pick.path, pick.generated_owner);
            if (element == nullptr) {
                return picked;
            }
            picked.instance = instance;
            picked.element = element;
            picked.size = window_size_for(world, canvas->window);
            return picked;
        }

        const Stylesheet *instance_sheet(const UiInstance &instance) {
            return instance.stylesheet ? &*instance.stylesheet : nullptr;
        }

    } // namespace

    std::size_t InspectorRowKeyHash::operator()(const InspectorRowKey &key) const noexcept {
        std::size_t hash = key.canvas.index;
        hash = hash * 131u + key.canvas.generation;
        hash ^= static_cast<std::size_t>(key.owner);
        for (const std::size_t step: key.relative) {
            hash = hash * 131u + step;
        }
        return hash;
    }

    std::string inspector_element_tag(const Element &element) {
        std::string tag = kind_name(element.kind);
        if (!element.id.empty()) {
            tag += '#';
            tag += element.id;
        }
        for (const std::string &class_name: element.classes) {
            tag += '.';
            tag += class_name;
        }
        return tag;
    }

    void set_inspector_attached(ecs::World &world, bool attached) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (inspector.attached == attached) {
            return;
        }
        inspector = UiInspector{};
        inspector.attached = attached;
    }

    bool inspector_attached(ecs::World &world) { return world.ctx<UiInspector>().attached; }

    InspectorPick inspector_selection(ecs::World &world, WindowId window) {
        const UiInspector &inspector = world.ctx<UiInspector>();
        const auto it = inspector.selection.find(window);
        if (it == inspector.selection.end()) {
            return {};
        }
        return it->second;
    }

    void inspector_retarget(ecs::World &world) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!inspector.attached) {
            return;
        }
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

    std::vector<InspectorTreeRow> inspector_tree(ecs::World &world) {
        std::vector<InspectorTreeRow> rows;
        if (!inspector_attached(world)) {
            return rows;
        }
        inspector_retarget(world);
        UiInspector &inspector = world.ctx<UiInspector>();
        const std::vector<CanvasSource> sources = live_canvases(world);
        std::unordered_set<WindowId> windows;
        for (const CanvasSource &source: sources) {
            windows.insert(source.window);
        }
        inspector.expansion.retain(
                [&world](const InspectorRowKey &key) { return world.try_get<UiInstance>(key.canvas) != nullptr; });

        std::vector<InspectorNode> roots;
        roots.reserve(sources.size());
        for (const CanvasSource &source: sources) {
            roots.push_back(InspectorNode{&world.get<UiInstance>(source.entity).document.root, source.entity,
                                          source.window});
        }
        const bool show_window = windows.size() > 1;
        const InspectorTreeSource tree_source;
        (void) flatten_tree(roots, inspector.expansion, tree_source,
                            [&](const InspectorNode &node, const TreeRowInfo &info) {
                                InspectorTreeRow row;
                                row.key = tree_source.key(node);
                                row.pick.canvas = node.canvas;
                                row.pick.path = node.path;
                                row.pick.generated_owner = node.owner;
                                row.pick.active = true;
                                row.window = node.window;
                                row.tree = info;
                                const auto pick_it = inspector.selection.find(node.window);
                                row.selected = pick_it != inspector.selection.end() && pick_it->second.active &&
                                               pick_it->second.canvas == node.canvas &&
                                               pick_it->second.path == node.path;
                                row.label = element_label(*node.element);
                                if (info.depth == 0 && show_window) {
                                    row.label.insert(0, std::format("[{}] ", static_cast<std::uint32_t>(node.window)));
                                }
                                rows.push_back(std::move(row));
                            });
        return rows;
    }

    void inspector_select(ecs::World &world, WindowId window, InspectorPick pick) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!inspector.attached) {
            return;
        }
        pick.active = true;
        inspector.detail_window = window;
        inspector.selection[window] = std::move(pick);
        ++inspector.selections;
    }

    void inspector_toggle(ecs::World &world, const InspectorRowKey &key) {
        UiInspector &inspector = world.ctx<UiInspector>();
        if (!inspector.attached) {
            return;
        }
        inspector.expansion.toggle(key);
    }

    std::string inspector_detail(ecs::World &world, const InspectorPick &pick) {
        if (!pick.active) {
            return "Nothing selected";
        }
        const PickedElement picked = resolve_pick(world, pick);
        if (picked.element == nullptr) {
            return "Selected element is not in the live tree.";
        }
        return describe_element(picked.instance->document.root, *picked.element, instance_sheet(*picked.instance),
                                static_cast<float>(picked.size.width), static_cast<float>(picked.size.height));
    }

    std::vector<std::string> inspector_rules(ecs::World &world, const InspectorPick &pick) {
        const PickedElement picked = resolve_pick(world, pick);
        if (picked.element == nullptr) {
            return {};
        }
        return rule_lines(picked.instance->document.root, *picked.element, instance_sheet(*picked.instance),
                          static_cast<float>(picked.size.width), static_cast<float>(picked.size.height));
    }

    std::optional<ecs::Entity> inspector_hover_canvas(ecs::World &world, WindowId window) {
        if (!inspector_attached(world)) {
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
        if (!any) {
            return std::nullopt;
        }
        return best;
    }

} // namespace engine::ui
