#include <engine/ui/document.h>

#include <algorithm>

namespace engine::ui {
namespace {

void layout_element(Element& element, const render::Rect& allocated);

[[nodiscard]] render::Rect inset_rect(const render::Rect& rect, const BoxInsets& padding) {
    return render::Rect{
            rect.x + padding.left,
            rect.y + padding.top,
            std::max(0.0f, rect.w - padding.left - padding.right),
            std::max(0.0f, rect.h - padding.top - padding.bottom),
    };
}

void layout_stack(Element& element, const render::Rect& allocated) {
    std::vector<Element*> children;
    children.reserve(element.children.size() + element.generated_items.size());
    if (element.kind == ElementKind::ItemsControl) {
        for (Element& child : element.generated_items) {
            children.push_back(&child);
        }
    } else {
        for (Element& child : element.children) {
            if (child.kind != ElementKind::ItemTemplate) {
                children.push_back(&child);
            }
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
    if (element.kind == ElementKind::Stack || element.kind == ElementKind::ItemsControl) {
        layout_stack(element, inset_rect(allocated, element.padding));
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
        if (vm.has_property(intern(*binding))) {
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
        ICommand* command = vm.find_command(intern(*element.command_binding));
        if (command == nullptr) {
            if (fatal != nullptr) {
                fatal->report("UI binding name is not registered: " + *element.command_binding);
            }
            return std::unexpected(UiError::MissingBinding);
        }
        element.command = command;
        element.disabled = !command->can_execute();
    }

    if (element.text_binding) {
        if (auto value = vm.read_property_string(intern(*element.text_binding))) {
            element.text = *value;
        }
    }
    if (element.content_binding) {
        if (auto value = vm.read_property_string(intern(*element.content_binding))) {
            element.text = *value;
        }
    }

    const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
    for (Element& child : element.children) {
        if (auto result = bind_element(child, vm, fatal, nested_template); !result) {
            return result;
        }
    }

    if (element.kind == ElementKind::ItemsControl && element.items_source_binding && !in_template) {
        element.generated_items.clear();
        const Element* tmpl = nullptr;
        for (const Element& child : element.children) {
            if (child.kind == ElementKind::ItemTemplate) {
                tmpl = &child;
                break;
            }
        }
        if (tmpl != nullptr) {
            const std::vector<ViewModel*> items = vm.read_item_source(intern(*element.items_source_binding));
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
