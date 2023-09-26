#include <engine/ui/document.h>

#include <algorithm>

namespace engine::ui {
namespace {

void layout_element(Element& element, const render::Rect& allocated);

void layout_stack(Element& element, const render::Rect& allocated) {
    std::vector<Element*> children;
    children.reserve(element.children.size());
    for (Element& child : element.children) {
        if (child.kind != ElementKind::ItemTemplate) {
            children.push_back(&child);
        }
    }
    if (children.empty()) {
        return;
    }

    const float gap_total = element.gap * static_cast<float>(children.size() - 1);
    if (element.direction == StackDirection::Horizontal) {
        const float width = std::max(0.0f, (allocated.w - gap_total) / static_cast<float>(children.size()));
        float x = allocated.x;
        for (Element* child : children) {
            layout_element(*child, render::Rect{x, allocated.y, width, allocated.h});
            x += width + element.gap;
        }
        return;
    }

    const float height = std::max(0.0f, (allocated.h - gap_total) / static_cast<float>(children.size()));
    float y = allocated.y;
    for (Element* child : children) {
        layout_element(*child, render::Rect{allocated.x, y, allocated.w, height});
        y += height + element.gap;
    }
}

void layout_element(Element& element, const render::Rect& allocated) {
    element.layout_rect = allocated;
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    if (element.kind == ElementKind::Stack) {
        layout_stack(element, allocated);
        return;
    }

    for (Element& child : element.children) {
        if (child.kind == ElementKind::ItemTemplate) {
            continue;
        }
        layout_element(child, allocated);
    }
}

std::expected<void, UiError> bind_element(Element& element, ViewModel& vm, IFatalError* fatal, bool in_template) {
    const auto require_property = [&](const std::optional<std::string>& binding) -> std::expected<void, UiError> {
        if (!binding) {
            return {};
        }
        if (in_template) {
            return {};
        }
        if (vm.has_property(*binding)) {
            return {};
        }
        if (fatal != nullptr) {
            fatal->report("UI binding name is not registered: " + *binding);
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

    if (element.command_binding && !in_template) {
        ICommand* command = vm.find_command(*element.command_binding);
        if (command == nullptr) {
            if (fatal != nullptr) {
                fatal->report("UI binding name is not registered: " + *element.command_binding);
            }
            return std::unexpected(UiError::MissingBinding);
        }
        element.command = command;
    }

    if (element.text_binding) {
        if (auto value = vm.read_property_string(*element.text_binding)) {
            element.text = *value;
        }
    }
    if (element.content_binding) {
        if (auto value = vm.read_property_string(*element.content_binding)) {
            element.text = *value;
        }
    }

    const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
    for (Element& child : element.children) {
        if (auto result = bind_element(child, vm, fatal, nested_template); !result) {
            return result;
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

void layout(UiDocument& document, const render::Rect& canvas_rect) {
    layout_element(document.root, canvas_rect);
}

Element* find_by_kind(Element& root, ElementKind kind) {
    return const_cast<Element*>(find_by_kind_const(root, kind));
}

const Element* find_by_kind(const Element& root, ElementKind kind) {
    return find_by_kind_const(root, kind);
}

}
