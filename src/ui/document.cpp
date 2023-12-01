#include <engine/ui/document.h>

#include "painter.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace engine::ui {
namespace {

constexpr float kDefaultImageSize = 32.0f;

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter);
[[nodiscard]] glm::vec2 compute_used(const Element& element, IUiPainter* painter);

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

[[nodiscard]] glm::vec2 measure_element_text(const Element& element, IUiPainter* painter) {
    if (painter != nullptr) {
        return painter->measure_text(element.text, element.font_family, element.font_size);
    }
    return fallback_measure_text(element.text, element.font_size);
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

[[nodiscard]] glm::vec2 intrinsic_size(const Element& element, IUiPainter* painter) {
    if (element.kind == ElementKind::Label || element.kind == ElementKind::Button) {
        const glm::vec2 text = measure_element_text(element, painter);
        return {
                element.padding.left + text.x + element.padding.right,
                element.padding.top + text.y + element.padding.bottom,
        };
    }
    if (element.kind == ElementKind::Image) {
        return {
                element.padding.left + kDefaultImageSize + element.padding.right,
                element.padding.top + kDefaultImageSize + element.padding.bottom,
        };
    }
    if (element.kind != ElementKind::Stack && element.kind != ElementKind::ItemsControl) {
        return {
                element.padding.left + element.padding.right,
                element.padding.top + element.padding.bottom,
        };
    }

    std::vector<const Element*> children;
    collect_layout_children(element, children);
    float main = 0.0f;
    float cross = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Element& child = *children[i];
        const glm::vec2 used = compute_used(child, painter);
        if (element.direction == StackDirection::Horizontal) {
            main += child.margin.left + used.x + child.margin.right;
            cross = std::max(cross, child.margin.top + used.y + child.margin.bottom);
        } else {
            main += child.margin.top + used.y + child.margin.bottom;
            cross = std::max(cross, child.margin.left + used.x + child.margin.right);
        }
        if (i + 1 < children.size()) {
            main += element.gap;
        }
    }
    if (element.direction == StackDirection::Horizontal) {
        return {
                element.padding.left + main + element.padding.right,
                element.padding.top + cross + element.padding.bottom,
        };
    }
    return {
            element.padding.left + cross + element.padding.right,
            element.padding.top + main + element.padding.bottom,
    };
}

glm::vec2 compute_used(const Element& element, IUiPainter* painter) {
    const glm::vec2 hug = intrinsic_size(element, painter);
    return {
            clamp_axis(element.width, element.min_width, hug.x),
            clamp_axis(element.height, element.min_height, hug.y),
    };
}

void layout_stack(Element& element, const render::Rect& allocated, IUiPainter* painter) {
    std::vector<Element*> children;
    collect_layout_children(element, children);
    if (children.empty()) {
        return;
    }

    float packed = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Element& child = *children[i];
        const glm::vec2 used = compute_used(child, painter);
        if (element.direction == StackDirection::Horizontal) {
            packed += child.margin.left + used.x + child.margin.right;
        } else {
            packed += child.margin.top + used.y + child.margin.bottom;
        }
        if (i + 1 < children.size()) {
            packed += element.gap;
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
        const glm::vec2 used = compute_used(child, painter);
        if (horizontal) {
            cursor += child.margin.left;
            const float extra =
                    std::max(0.0f, allocated.h - child.margin.top - child.margin.bottom - used.y);
            float y = allocated.y + child.margin.top;
            if (element.align_items == UiAlign::Center) {
                y += extra * 0.5f;
            } else if (element.align_items == UiAlign::End) {
                y += extra;
            }
            layout_element(child, render::Rect{cursor, y, used.x, used.y}, painter);
            cursor += used.x + child.margin.right;
        } else {
            cursor += child.margin.top;
            const float extra =
                    std::max(0.0f, allocated.w - child.margin.left - child.margin.right - used.x);
            float x = allocated.x + child.margin.left;
            if (element.align_items == UiAlign::Center) {
                x += extra * 0.5f;
            } else if (element.align_items == UiAlign::End) {
                x += extra;
            }
            layout_element(child, render::Rect{x, cursor, used.x, used.y}, painter);
            cursor += used.y + child.margin.bottom;
        }
        if (i + 1 < children.size()) {
            cursor += element.gap;
        }
    }
}

void layout_element(Element& element, const render::Rect& box, IUiPainter* painter) {
    element.layout_rect = box;
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    if (element.kind == ElementKind::Stack || element.kind == ElementKind::ItemsControl) {
        layout_stack(element, inset_rect(box, element.padding), painter);
        return;
    }

    const render::Rect content = inset_rect(box, element.padding);
    for (Element& child : element.children) {
        if (child.kind == ElementKind::ItemTemplate) {
            continue;
        }
        const glm::vec2 used = compute_used(child, painter);
        layout_element(child,
                render::Rect{content.x + child.margin.left, content.y + child.margin.top, used.x, used.y},
                painter);
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
    layout_element(document.root, canvas_rect, painter);
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
