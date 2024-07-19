#include <engine/ui/document.h>

#include "painter.h"

#include <engine/ui/canvas.h>  // rect_contains

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace engine::ui {
namespace {

constexpr float kDefaultImageSize = 32.0f;

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter, glm::vec2 parent_content,
        const render::Rect& containing_block);
void layout_absolute(Element& element, const render::Rect& containing_block, IUiPainter* painter);
[[nodiscard]] glm::vec2 compute_used(const Element& element, IUiPainter* painter, glm::vec2 parent_content);

// position: relative never reflows siblings (they already packed/cursor'd against the
// pre-offset size) - it only nudges this element's own already-placed layout_rect.
void apply_relative_offset(Element& element, glm::vec2 basis, float em_basis) {
    if (element.position != PositionMode::Relative) {
        return;
    }
    float dx = 0.0f;
    if (element.inset_left) {
        dx = resolve_length(*element.inset_left, basis.x, em_basis);
    } else if (element.inset_right) {
        dx = -resolve_length(*element.inset_right, basis.x, em_basis);
    }
    float dy = 0.0f;
    if (element.inset_top) {
        dy = resolve_length(*element.inset_top, basis.y, em_basis);
    } else if (element.inset_bottom) {
        dy = -resolve_length(*element.inset_bottom, basis.y, em_basis);
    }
    element.layout_rect.x += dx;
    element.layout_rect.y += dy;
}

struct ResolvedBox {
    float gap = 0.0f;
    BoxInsets padding{};
    BoxInsets margin{};
    std::optional<float> width;
    std::optional<float> height;
    std::optional<float> min_width;
    std::optional<float> min_height;
    float font_size = kDefaultFontSize;
};

[[nodiscard]] ResolvedBox resolve_box(const Element& element, glm::vec2 parent_content) {
    ResolvedBox box;
    box.font_size = resolve_font_size(element.font_size, parent_content.x);
    box.padding = BoxInsets{
            resolve_length(element.padding.top, parent_content.y, box.font_size),
            resolve_length(element.padding.right, parent_content.x, box.font_size),
            resolve_length(element.padding.bottom, parent_content.y, box.font_size),
            resolve_length(element.padding.left, parent_content.x, box.font_size),
    };
    box.margin = BoxInsets{
            resolve_length(element.margin.top, parent_content.y, box.font_size),
            resolve_length(element.margin.right, parent_content.x, box.font_size),
            resolve_length(element.margin.bottom, parent_content.y, box.font_size),
            resolve_length(element.margin.left, parent_content.x, box.font_size),
    };
    box.gap = resolve_length(element.gap, parent_content.x, box.font_size);
    if (element.width) {
        box.width = resolve_length(*element.width, parent_content.x, box.font_size);
    }
    if (element.height) {
        box.height = resolve_length(*element.height, parent_content.y, box.font_size);
    }
    if (element.min_width) {
        box.min_width = resolve_length(*element.min_width, parent_content.x, box.font_size);
    }
    if (element.min_height) {
        box.min_height = resolve_length(*element.min_height, parent_content.y, box.font_size);
    }
    return box;
}

[[nodiscard]] glm::vec2 content_basis(const ResolvedBox& box) {
    return {
            box.width ? std::max(0.0f, *box.width - box.padding.left - box.padding.right) : 0.0f,
            box.height ? std::max(0.0f, *box.height - box.padding.top - box.padding.bottom) : 0.0f,
    };
}

[[nodiscard]] render::Rect inset_rect(const render::Rect& rect, const BoxInsets& padding) {
    return render::Rect{
            rect.x + padding.left,
            rect.y + padding.top,
            std::max(0.0f, rect.w - padding.left - padding.right),
            std::max(0.0f, rect.h - padding.top - padding.bottom),
    };
}

[[nodiscard]] glm::vec2 fallback_measure_text(std::string_view text, float size) {
    return {static_cast<float>(text.size()) * size * 0.5f, size};
}

[[nodiscard]] glm::vec2 measure_element_text(const Element& element, IUiPainter* painter, float font_size) {
    if (painter != nullptr) {
        return painter->measure_text(element.text, element.font_family, font_size);
    }
    return fallback_measure_text(element.text, font_size);
}

[[nodiscard]] float clamp_axis(std::optional<float> specified, std::optional<float> min_size, float hug) {
    float value = specified.value_or(hug);
    if (min_size) {
        value = std::max(value, *min_size);
    }
    return std::max(0.0f, value);
}

template<typename ElementT, typename Out>
void collect_layout_children(ElementT& element, std::vector<Out*>& children) {
    children.clear();
    if (element.kind == ElementKind::ItemsControl) {
        children.reserve(element.generated_items.size());
        for (auto& child : element.generated_items) {
            children.push_back(&child);
        }
        return;
    }
    children.reserve(element.children.size());
    for (auto& child : element.children) {
        if (child.kind != ElementKind::ItemTemplate) {
            children.push_back(&child);
        }
    }
}

[[nodiscard]] glm::vec2 intrinsic_size(const Element& element, IUiPainter* painter, glm::vec2 parent_content) {
    const ResolvedBox box = resolve_box(element, parent_content);
    if (element.kind == ElementKind::Label || element.kind == ElementKind::Button) {
        const glm::vec2 text = measure_element_text(element, painter, box.font_size);
        return {
                box.padding.left + text.x + box.padding.right,
                box.padding.top + text.y + box.padding.bottom,
        };
    }
    if (element.kind == ElementKind::Image) {
        return {
                box.padding.left + kDefaultImageSize + box.padding.right,
                box.padding.top + kDefaultImageSize + box.padding.bottom,
        };
    }
    if (element.kind != ElementKind::Stack && element.kind != ElementKind::ItemsControl) {
        return {
                box.padding.left + box.padding.right,
                box.padding.top + box.padding.bottom,
        };
    }

    const glm::vec2 child_basis = content_basis(box);
    std::vector<const Element*> children;
    collect_layout_children(element, children);
    float main = 0.0f;
    float cross = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Element& child = *children[i];
        const ResolvedBox child_box = resolve_box(child, child_basis);
        const glm::vec2 used = compute_used(child, painter, child_basis);
        if (element.direction == StackDirection::Horizontal) {
            main += child_box.margin.left + used.x + child_box.margin.right;
            cross = std::max(cross, child_box.margin.top + used.y + child_box.margin.bottom);
        } else {
            main += child_box.margin.top + used.y + child_box.margin.bottom;
            cross = std::max(cross, child_box.margin.left + used.x + child_box.margin.right);
        }
        if (i + 1 < children.size()) {
            main += box.gap;
        }
    }
    if (element.direction == StackDirection::Horizontal) {
        return {
                box.padding.left + main + box.padding.right,
                box.padding.top + cross + box.padding.bottom,
        };
    }
    return {
            box.padding.left + cross + box.padding.right,
            box.padding.top + main + box.padding.bottom,
    };
}

glm::vec2 compute_used(const Element& element, IUiPainter* painter, glm::vec2 parent_content) {
    const ResolvedBox box = resolve_box(element, parent_content);
    const glm::vec2 hug = intrinsic_size(element, painter, parent_content);
    return {
            clamp_axis(box.width, box.min_width, hug.x),
            clamp_axis(box.height, box.min_height, hug.y),
    };
}

// position: absolute is resolved against `containing_block` (the nearest ancestor with
// position != Static, or the canvas root) rather than packed into the normal flow. Explicit or
// hug size is used by default; when both opposite insets are set and no explicit size on that
// axis, the box stretches to fill instead.
void layout_absolute(Element& element, const render::Rect& containing_block, IUiPainter* painter) {
    const glm::vec2 basis{containing_block.w, containing_block.h};
    const ResolvedBox box = resolve_box(element, basis);

    std::optional<float> left;
    std::optional<float> right;
    std::optional<float> top;
    std::optional<float> bottom;
    if (element.inset_left) {
        left = resolve_length(*element.inset_left, basis.x, box.font_size);
    }
    if (element.inset_right) {
        right = resolve_length(*element.inset_right, basis.x, box.font_size);
    }
    if (element.inset_top) {
        top = resolve_length(*element.inset_top, basis.y, box.font_size);
    }
    if (element.inset_bottom) {
        bottom = resolve_length(*element.inset_bottom, basis.y, box.font_size);
    }

    glm::vec2 used = compute_used(element, painter, basis);
    if (!box.width && left && right) {
        used.x = std::max(0.0f, containing_block.w - *left - *right);
    }
    if (!box.height && top && bottom) {
        used.y = std::max(0.0f, containing_block.h - *top - *bottom);
    }

    float x = containing_block.x;
    if (left) {
        x += *left;
    } else if (right) {
        x += containing_block.w - *right - used.x;
    }
    float y = containing_block.y;
    if (top) {
        y += *top;
    } else if (bottom) {
        y += containing_block.h - *bottom - used.y;
    }

    layout_element(element, render::Rect{x, y, used.x, used.y}, painter, basis, containing_block);
}

void layout_stack(Element& element, const render::Rect& allocated, IUiPainter* painter, const ResolvedBox& self,
        const render::Rect& containing_block) {
    std::vector<Element*> children;
    collect_layout_children(element, children);
    if (children.empty()) {
        return;
    }

    std::vector<Element*> flow;
    std::vector<Element*> absolute;
    flow.reserve(children.size());
    for (Element* child : children) {
        if (child->position == PositionMode::Absolute) {
            absolute.push_back(child);
        } else {
            flow.push_back(child);
        }
    }

    const glm::vec2 child_basis{allocated.w, allocated.h};
    if (!flow.empty()) {
        float packed = 0.0f;
        for (std::size_t i = 0; i < flow.size(); ++i) {
            const Element& child = *flow[i];
            const ResolvedBox child_box = resolve_box(child, child_basis);
            const glm::vec2 used = compute_used(child, painter, child_basis);
            if (element.direction == StackDirection::Horizontal) {
                packed += child_box.margin.left + used.x + child_box.margin.right;
            } else {
                packed += child_box.margin.top + used.y + child_box.margin.bottom;
            }
            if (i + 1 < flow.size()) {
                packed += self.gap;
            }
        }

        const bool horizontal = element.direction == StackDirection::Horizontal;
        const float leftover = std::max(0.0f, (horizontal ? allocated.w : allocated.h) - packed);
        float cursor = horizontal ? allocated.x : allocated.y;
        if (element.justify == UiAlign::Center) {
            cursor += leftover * 0.5f;
        } else if (element.justify == UiAlign::End) {
            cursor += leftover;
        }

        for (std::size_t i = 0; i < flow.size(); ++i) {
            Element& child = *flow[i];
            const ResolvedBox child_box = resolve_box(child, child_basis);
            const glm::vec2 used = compute_used(child, painter, child_basis);
            if (horizontal) {
                cursor += child_box.margin.left;
                const float extra =
                        std::max(0.0f, allocated.h - child_box.margin.top - child_box.margin.bottom - used.y);
                float y = allocated.y + child_box.margin.top;
                if (element.align_items == UiAlign::Center) {
                    y += extra * 0.5f;
                } else if (element.align_items == UiAlign::End) {
                    y += extra;
                }
                layout_element(child, render::Rect{cursor, y, used.x, used.y}, painter, child_basis, containing_block);
                apply_relative_offset(child, child_basis, child_box.font_size);
                cursor += used.x + child_box.margin.right;
            } else {
                cursor += child_box.margin.top;
                const float extra =
                        std::max(0.0f, allocated.w - child_box.margin.left - child_box.margin.right - used.x);
                float x = allocated.x + child_box.margin.left;
                if (element.align_items == UiAlign::Center) {
                    x += extra * 0.5f;
                } else if (element.align_items == UiAlign::End) {
                    x += extra;
                }
                layout_element(child, render::Rect{x, cursor, used.x, used.y}, painter, child_basis, containing_block);
                apply_relative_offset(child, child_basis, child_box.font_size);
                cursor += used.y + child_box.margin.bottom;
            }
            if (i + 1 < flow.size()) {
                cursor += self.gap;
            }
        }
    }

    for (Element* child : absolute) {
        layout_absolute(*child, containing_block, painter);
    }
}

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter, glm::vec2 parent_content,
        const render::Rect& containing_block) {
    element.layout_rect = box;
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    const ResolvedBox resolved = resolve_box(element, parent_content);
    const render::Rect content = inset_rect(box, resolved.padding);
    const glm::vec2 child_basis{content.w, content.h};
    // A positioned element (relative or absolute) becomes the containing block its own
    // descendants resolve `position: absolute` against.
    const render::Rect child_containing_block = element.position != PositionMode::Static ? box : containing_block;
    if (element.kind == ElementKind::Stack || element.kind == ElementKind::ItemsControl) {
        layout_stack(element, content, painter, resolved, child_containing_block);
        return;
    }

    std::vector<Element*> flow;
    std::vector<Element*> absolute;
    for (Element& child : element.children) {
        if (child.kind == ElementKind::ItemTemplate) {
            continue;
        }
        if (child.position == PositionMode::Absolute) {
            absolute.push_back(&child);
        } else {
            flow.push_back(&child);
        }
    }
    for (Element* child_ptr : flow) {
        Element& child = *child_ptr;
        const ResolvedBox child_box = resolve_box(child, child_basis);
        const glm::vec2 used = compute_used(child, painter, child_basis);
        layout_element(child,
                render::Rect{content.x + child_box.margin.left, content.y + child_box.margin.top, used.x, used.y},
                painter, child_basis, child_containing_block);
        apply_relative_offset(child, child_basis, child_box.font_size);
    }
    for (Element* child_ptr : absolute) {
        layout_absolute(*child_ptr, child_containing_block, painter);
    }
}

std::expected<void, UiError> bind_element(Element& element, ViewModel& vm, IFatalError* fatal, bool in_template) {
    const auto require_property = [&](BindingId binding) -> std::expected<void, UiError> {
        if (!is_bound(binding)) {
            return {};
        }
        if (in_template) {
            return {};
        }
        if (vm.has_property(binding)) {
            return {};
        }
        if (fatal != nullptr) {
            fatal->report("UI binding name is not registered");
        }
        return std::unexpected(UiError::MissingBinding);
    };

    if (auto result = require_property(element.text_binding); !result) {
        return result;
    }
    if (auto result = require_property(element.content_binding); !result) {
        return result;
    }
    if (auto result = require_property(element.source_binding); !result) {
        return result;
    }
    if (auto result = require_property(element.items_source_binding); !result) {
        return result;
    }

    if (is_bound(element.command_binding) && !in_template) {
        ICommand* command = vm.find_command(element.command_binding);
        if (command == nullptr) {
            if (fatal != nullptr) {
                fatal->report("UI binding name is not registered");
            }
            return std::unexpected(UiError::MissingBinding);
        }
        element.command = command;
        element.disabled = !command->can_execute();
    }

    if (is_bound(element.text_binding)) {
        if (auto value = vm.read_property_string(element.text_binding)) {
            element.text = *value;
        }
    }
    if (is_bound(element.content_binding)) {
        if (auto value = vm.read_property_string(element.content_binding)) {
            element.text = *value;
        }
    }
    if (is_bound(element.source_binding) && !in_template) {
        const auto value = vm.read_property_asset_id(element.source_binding);
        if (!value) {
            if (fatal != nullptr) {
                fatal->report("UI binding name is not registered");
            }
            return std::unexpected(UiError::MissingBinding);
        }
        element.source = *value;
    }

    const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
    for (Element& child : element.children) {
        if (auto result = bind_element(child, vm, fatal, nested_template); !result) {
            return result;
        }
    }

    if (element.kind == ElementKind::ItemsControl && is_bound(element.items_source_binding) && !in_template) {
        element.generated_items.clear();
        const Element* tmpl = nullptr;
        for (const Element& child : element.children) {
            if (child.kind == ElementKind::ItemTemplate) {
                tmpl = &child;
                break;
            }
        }
        if (tmpl != nullptr) {
            const std::vector<ViewModel*> items = vm.read_item_source(element.items_source_binding);
            for (ViewModel* item : items) {
                if (item == nullptr) {
                    continue;
                }
                if (tmpl->children.empty()) {
                    Element clone = *tmpl;
                    clone.kind = ElementKind::Stack;
                    clone.children.clear();
                    if (auto result = bind_element(clone, *item, fatal, false); !result) {
                        return result;
                    }
                    element.generated_items.push_back(std::move(clone));
                    continue;
                }
                for (const Element& node : tmpl->children) {
                    Element clone = node;
                    clone.generated_items.clear();
                    if (auto result = bind_element(clone, *item, fatal, false); !result) {
                        return result;
                    }
                    element.generated_items.push_back(std::move(clone));
                }
            }
        }
    }
    return {};
}

const Element* find_by_kind_const(const Element& root, ElementKind kind) {
    if (root.kind == kind) {
        return &root;
    }
    for (const Element& child : root.children) {
        if (const Element* found = find_by_kind_const(child, kind)) {
            return found;
        }
    }
    return nullptr;
}

}

std::expected<void, UiError> apply_bindings(UiDocument& document, ViewModel& data_context, IFatalError* fatal) {
    return bind_element(document.root, data_context, fatal, false);
}

void layout(UiDocument& document, const render::Rect& canvas_rect, IUiPainter* painter) {
    layout_element(document.root, canvas_rect, painter, glm::vec2{canvas_rect.w, canvas_rect.h}, canvas_rect);
}

void layout(UiDocument& document, const render::Rect& canvas_rect) {
    layout(document, canvas_rect, nullptr);
}

render::Rect hit_bounds(const Element& element) {
    if (element.rotation_deg == 0.0f && element.scale == 1.0f) {
        return element.layout_rect;
    }
    const render::Rect& rect = element.layout_rect;
    const glm::vec2 center{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
    const float radians = element.rotation_deg * (3.14159265358979323846f / 180.0f);
    const float cos_r = std::cos(radians);
    const float sin_r = std::sin(radians);
    const glm::vec2 half{rect.w * 0.5f * element.scale, rect.h * 0.5f * element.scale};
    const glm::vec2 corners[4] = {
            {-half.x, -half.y},
            {half.x, -half.y},
            {half.x, half.y},
            {-half.x, half.y},
    };
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    for (const glm::vec2& corner : corners) {
        const float x = corner.x * cos_r - corner.y * sin_r;
        const float y = corner.x * sin_r + corner.y * cos_r;
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    return render::Rect{center.x + min_x, center.y + min_y, max_x - min_x, max_y - min_y};
}

std::vector<Element*> child_stacking_order(std::vector<Element>& children) {
    std::vector<Element*> order;
    order.reserve(children.size());
    for (Element& child : children) {
        order.push_back(&child);
    }
    std::stable_sort(order.begin(), order.end(),
            [](const Element* a, const Element* b) { return a->z_index < b->z_index; });
    return order;
}

Element* hit_test(Element& element, float x, float y) {
    if (!rect_contains(hit_bounds(element), x, y)) {
        return nullptr;
    }
    // child_stacking_order() is ascending (paint order); iterating its result back-to-front
    // visits the topmost (highest z-index / last-drawn) sibling first.
    std::vector<Element*> children = child_stacking_order(element.children);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Element* nested = hit_test(**it, x, y)) {
            return nested;
        }
    }
    std::vector<Element*> generated = child_stacking_order(element.generated_items);
    for (auto it = generated.rbegin(); it != generated.rend(); ++it) {
        if (Element* nested = hit_test(**it, x, y)) {
            return nested;
        }
    }
    if (element.kind == ElementKind::Button) {
        return &element;
    }
    return nullptr;
}

Element* find_by_kind(Element& root, ElementKind kind) {
    return const_cast<Element*>(find_by_kind_const(root, kind));
}

const Element* find_by_kind(const Element& root, ElementKind kind) {
    return find_by_kind_const(root, kind);
}

}
