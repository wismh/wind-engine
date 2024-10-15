#include <engine/ui/document.h>

#include "painter.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace engine::ui {
namespace {

constexpr float kDefaultImageSize = 32.0f;

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter, glm::vec2 parent_content);
[[nodiscard]] glm::vec2 compute_used(const Element& element, IUiPainter* painter, glm::vec2 parent_content);

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

void layout_stack(Element& element, const render::Rect& allocated, IUiPainter* painter, const ResolvedBox& self) {
    std::vector<Element*> children;
    collect_layout_children(element, children);
    if (children.empty()) {
        return;
    }

    const glm::vec2 child_basis{allocated.w, allocated.h};
    float packed = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Element& child = *children[i];
        const ResolvedBox child_box = resolve_box(child, child_basis);
        const glm::vec2 used = compute_used(child, painter, child_basis);
        if (element.direction == StackDirection::Horizontal) {
            packed += child_box.margin.left + used.x + child_box.margin.right;
        } else {
            packed += child_box.margin.top + used.y + child_box.margin.bottom;
        }
        if (i + 1 < children.size()) {
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

    for (std::size_t i = 0; i < children.size(); ++i) {
        Element& child = *children[i];
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
            layout_element(child, render::Rect{cursor, y, used.x, used.y}, painter, child_basis);
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
            layout_element(child, render::Rect{x, cursor, used.x, used.y}, painter, child_basis);
            cursor += used.y + child_box.margin.bottom;
        }
        if (i + 1 < children.size()) {
            cursor += self.gap;
        }
    }
}

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter, glm::vec2 parent_content) {
    element.layout_rect = box;
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    const ResolvedBox resolved = resolve_box(element, parent_content);
    const render::Rect content = inset_rect(box, resolved.padding);
    const glm::vec2 child_basis{content.w, content.h};
    if (element.kind == ElementKind::Stack || element.kind == ElementKind::ItemsControl) {
        layout_stack(element, content, painter, resolved);
        return;
    }

    for (Element& child : element.children) {
        if (child.kind == ElementKind::ItemTemplate) {
            continue;
        }
        const ResolvedBox child_box = resolve_box(child, child_basis);
        const glm::vec2 used = compute_used(child, painter, child_basis);
        layout_element(child,
                render::Rect{content.x + child_box.margin.left, content.y + child_box.margin.top, used.x, used.y},
                painter, child_basis);
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
    layout_element(document.root, canvas_rect, painter, glm::vec2{canvas_rect.w, canvas_rect.h});
}

void layout(UiDocument& document, const render::Rect& canvas_rect) {
    layout(document, canvas_rect, nullptr);
}

Element* find_by_kind(Element& root, ElementKind kind) {
    return const_cast<Element*>(find_by_kind_const(root, kind));
}

const Element* find_by_kind(const Element& root, ElementKind kind) {
    return find_by_kind_const(root, kind);
}

}
