#pragma once

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

enum class UiError {
    InvalidMarkup,
    UnknownElement,
    MissingBinding,
    ForbiddenContent,
};

enum class ElementKind {
    Canvas,
    Stack,
    Label,
    Button,
    Image,
    ItemsControl,
    ItemTemplate,
};

enum class StackDirection {
    Vertical,
    Horizontal,
};

struct BoxInsets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

struct Element {
    ElementKind kind = ElementKind::Canvas;
    std::string id;
    std::string class_name;
    std::string name;

    std::string text;
    BindingId text_binding{};
    BindingId content_binding{};
    BindingId command_binding{};
    BindingId source_binding{};
    BindingId items_source_binding{};
    std::optional<AssetId> source;

    StackDirection direction = StackDirection::Vertical;
    float gap = 0.0f;
    BoxInsets padding{};

    render::Rect layout_rect{};
    ICommand* command = nullptr;
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    std::vector<Element> children;
    std::vector<Element> generated_items;
};

struct UiDocument {
    Element root;
    std::optional<AssetId> stylesheet;
};

struct UiInstance {
    UiDocument document;
    std::optional<Stylesheet> stylesheet;
};

[[nodiscard]] std::expected<UiDocument, UiError> parse_xml(
        std::string_view xml, IFatalError* fatal = nullptr, const ViewModel* data_context = nullptr);

std::expected<void, UiError> apply_bindings(UiDocument& document, ViewModel& data_context, IFatalError* fatal = nullptr);

void layout(UiDocument& document, const render::Rect& canvas_rect);

[[nodiscard]] Element* find_by_kind(Element& root, ElementKind kind);
[[nodiscard]] const Element* find_by_kind(const Element& root, ElementKind kind);

}
