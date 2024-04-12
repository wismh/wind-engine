#pragma once

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <cstddef>
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

enum class UiAlign {
    Start,
    Center,
    End,
};

enum class PositionMode {
    Static,
    Relative,
    Absolute,
};

struct BoxInsets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

enum class LengthUnit {
    Px,
    Percent,
    Em,
};

enum class CalcOp {
    Add,
    Sub,
    Mul,
    Div,
};

struct CalcNode {
    enum class Kind {
        Literal,
        Binary,
    };

    Kind kind = Kind::Literal;
    float value = 0.0f;
    LengthUnit unit = LengthUnit::Px;
    CalcOp op = CalcOp::Add;
    std::size_t left = 0;
    std::size_t right = 0;
};

struct Length {
    float value = 0.0f;
    LengthUnit unit = LengthUnit::Px;
    std::vector<CalcNode> calc;
};

struct LengthInsets {
    Length top{};
    Length right{};
    Length bottom{};
    Length left{};
};

constexpr float kDefaultFontSize = 16.0f;

[[nodiscard]] inline float resolve_literal(float value, LengthUnit unit, float percent_basis, float em_basis) noexcept {
    switch (unit) {
        case LengthUnit::Px:
            return value;
        case LengthUnit::Percent:
            return percent_basis * (value / 100.0f);
        case LengthUnit::Em:
            return em_basis * value;
    }
    return value;
}

[[nodiscard]] inline float resolve_calc_node(
        const std::vector<CalcNode>& nodes, std::size_t index, float percent_basis, float em_basis) noexcept {
    if (index >= nodes.size()) {
        return 0.0f;
    }
    const CalcNode& node = nodes[index];
    if (node.kind != CalcNode::Kind::Binary) {
        return resolve_literal(node.value, node.unit, percent_basis, em_basis);
    }
    const float lhs = resolve_calc_node(nodes, node.left, percent_basis, em_basis);
    const float rhs = resolve_calc_node(nodes, node.right, percent_basis, em_basis);
    switch (node.op) {
        case CalcOp::Add:
            return lhs + rhs;
        case CalcOp::Sub:
            return lhs - rhs;
        case CalcOp::Mul:
            return lhs * rhs;
        case CalcOp::Div:
            return rhs == 0.0f ? 0.0f : lhs / rhs;
    }
    return 0.0f;
}

[[nodiscard]] inline float resolve_length(const Length& length, float percent_basis, float em_basis) noexcept {
    if (!length.calc.empty()) {
        return resolve_calc_node(length.calc, length.calc.size() - 1, percent_basis, em_basis);
    }
    return resolve_literal(length.value, length.unit, percent_basis, em_basis);
}

[[nodiscard]] inline float resolve_font_size(const Length& font_size, float percent_basis) noexcept {
    if (font_size.calc.empty() && font_size.unit == LengthUnit::Em) {
        return font_size.value * kDefaultFontSize;
    }
    return resolve_length(font_size, percent_basis, kDefaultFontSize);
}

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
    Length gap{};
    LengthInsets padding{};
    LengthInsets margin{};
    std::optional<Length> width;
    std::optional<Length> height;
    std::optional<Length> min_width;
    std::optional<Length> min_height;
    UiAlign justify = UiAlign::Start;
    UiAlign align_items = UiAlign::Start;
    UiAlign text_align = UiAlign::Start;
    Length font_size{kDefaultFontSize, LengthUnit::Px};
    AssetId font_family{};
    float animation_elapsed = 0.0f;
    int z_index = 0;
    PositionMode position = PositionMode::Static;
    std::optional<Length> inset_top;
    std::optional<Length> inset_right;
    std::optional<Length> inset_bottom;
    std::optional<Length> inset_left;
    float rotation_deg = 0.0f;
    float scale = 1.0f;

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
    AssetId loaded_document{};
    std::optional<AssetId> loaded_stylesheet;
    std::vector<AssetId> loaded_extra_stylesheets;
    std::vector<AssetId> loaded_sheet_ids;
    ViewModel* loaded_data_context = nullptr;
};

[[nodiscard]] std::expected<UiDocument, UiError> parse_xml(
        std::string_view xml, IFatalError* fatal = nullptr, const ViewModel* data_context = nullptr);

std::expected<void, UiError> apply_bindings(UiDocument& document, ViewModel& data_context, IFatalError* fatal = nullptr);

void layout(UiDocument& document, const render::Rect& canvas_rect);

[[nodiscard]] Element* find_by_kind(Element& root, ElementKind kind);
[[nodiscard]] const Element* find_by_kind(const Element& root, ElementKind kind);

// Sibling paint/hit-test order: stable sort by z_index ascending (low first = behind, matching
// UiCanvas::order), tie-broken by document order. z_index == 0 everywhere (the default) leaves
// order unchanged. Paint iterates this forward; hit-testing iterates it in reverse (topmost
// first).
[[nodiscard]] std::vector<Element*> child_stacking_order(std::vector<Element>& children);

// Hit-test bounds: layout_rect for an untransformed element (the common case, byte-identical to
// today), otherwise the axis-aligned bounding box of the element's rotated+scaled corners about
// its own center. This is an AABB approximation, not a precise oriented-rect test - a rotated
// element's hit area is slightly generous at its corners.
[[nodiscard]] render::Rect hit_bounds(const Element& element);

// Topmost Button under (x, y): prunes by hit_bounds() containment, visits siblings in reverse
// stacking order (highest z-index / last-drawn first), returns the first Button found or
// nullptr. Shared by click resolution (canvas.cpp) and hover resolution (paint.cpp) so both
// agree on which element is "on top."
[[nodiscard]] Element* hit_test(Element& root, float x, float y);

}
