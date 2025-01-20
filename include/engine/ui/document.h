#pragma once

#include <engine/builtin_ids.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::ui {

enum class UiError {
    InvalidMarkup,
    UnknownElement,
    MissingBinding,
    ForbiddenContent,
    Io,
    CyclicInclude,
};

enum class ElementKind {
    Canvas,
    Stack,
    Label,
    Button,
    Image,
    ItemsControl,
    ItemTemplate,
    Line,
    Component,
    Viewport,
    TextInput,
    ScrollView,
};

enum class Overflow {
    Visible,
    Hidden,
    Scroll,
    Auto,
};

enum class StackDirection {
    Vertical,
    Horizontal,
};

enum class UiAlign {
    Start,
    Center,
    End,
    // justify-content only (parse_align() never returns this for align-items/text-align; those
    // ignore it and fall back to Start-equivalent behavior, same as CSS align-items has no
    // space-between). First child flush to the start, last flush to the end, leftover space split
    // evenly between the rest. A single child behaves like Start (nothing to space against).
    SpaceBetween,
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
constexpr float kViewportMinZoom = 0.25f;
constexpr float kViewportMaxZoom = 4.0f;
constexpr float kViewportZoomStep = 1.1f;

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

// A `var-<name>="{binding path}"` XML attribute: resolved every frame in bind_element into
// Element::custom_properties, then substituted for `var(--<name>)` references in CSS declarations
// (paint.cpp) — lets a stylesheet property (color, position, ...) read arbitrary VM data without a
// dedicated `_binding` field per property.
struct CustomPropertyBinding {
    std::string name;
    BindingId binding{};
};

enum class BackgroundRepeat {
    NoRepeat,
    Repeat,
};

// The fully-cascaded result of matching an Element against a Stylesheet (paint.cpp's
// compute_style()). Lives here rather than as a paint.cpp-private type only so Element can cache
// it (see StyleCacheEntry below) without a public header including a private one — every field
// here is already a type Element itself exposes (glm, AssetId, Length/LengthInsets/UiAlign/...),
// so this adds no SDL/glad/NanoVG/spdlog/tinyxml2/mixer exposure.
struct ComputedStyle {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 background{0.0f, 0.0f, 0.0f, 0.0f};
    std::optional<AssetId> background_image;
    std::optional<LengthInsets> background_slice;
    BackgroundRepeat background_repeat = BackgroundRepeat::NoRepeat;
    float opacity = 1.0f;
    bool visible = true;
    Length gap{};
    bool has_gap = false;
    StackDirection direction = StackDirection::Vertical;
    bool has_direction = false;
    LengthInsets padding{};
    LengthInsets margin{};
    std::optional<Length> width;
    std::optional<Length> height;
    std::optional<Length> min_width;
    std::optional<Length> min_height;
    UiAlign justify = UiAlign::Start;
    UiAlign align_items = UiAlign::Start;
    UiAlign text_align = UiAlign::Start;
    Length border_radius{};
    Length border_width{};
    glm::vec4 border_color{0.0f, 0.0f, 0.0f, 0.0f};
    // Line endpoints, offsets from the element's own layout_rect origin. Only meaningful when
    // element.kind == ElementKind::Line.
    Length x1{};
    Length y1{};
    Length x2{};
    Length y2{};
    Length stroke_width{2.0f, LengthUnit::Px};
    glm::vec4 stroke{0.0f, 0.0f, 0.0f, 1.0f};
    Length font_size{kDefaultFontSize, LengthUnit::Px};
    AssetId font_family = builtin::font_ui;
    std::string animation_name;
    float animation_duration = 0.0f;
    int z_index = 0;
    PositionMode position = PositionMode::Static;
    std::optional<Length> inset_top;
    std::optional<Length> inset_right;
    std::optional<Length> inset_bottom;
    std::optional<Length> inset_left;
    float rotation_deg = 0.0f;
    float scale = 1.0f;
    Overflow overflow_x = Overflow::Visible;
    Overflow overflow_y = Overflow::Visible;
    bool has_overflow_x = false;
    bool has_overflow_y = false;
    std::optional<Length> scrollbar_width;
    bool has_scrollbar_width = false;
    glm::vec4 scrollbar_track_color{0.0f, 0.0f, 0.0f, 0.0f};
    bool has_scrollbar_track_color = false;
    glm::vec4 scrollbar_thumb_color{0.4f, 0.4f, 0.4f, 0.8f};
    bool has_scrollbar_thumb_color = false;
    glm::vec4 scrollbar_thumb_hover_color{0.6f, 0.6f, 0.6f, 1.0f};
    bool has_scrollbar_thumb_hover_color = false;
    Length scrollbar_border_radius{4.0f, LengthUnit::Px};
    bool has_scrollbar_border_radius = false;
    // Cascaded `--name: value;` declarations, keyed without the leading `--`. Consulted by
    // resolve_var() when a declaration's value is `var(--name)` and the element itself has no
    // matching entry in Element::custom_properties (per-instance, VM-bound — takes priority).
    std::unordered_map<std::string, std::string> custom_properties;
};

struct Element;

// Memoizes one compute_style() call (paint.cpp) for one Element + one allow_pseudo variant.
// apply_layout_style() always calls with allow_pseudo=false, paint_element() always with
// allow_pseudo=true — Element keeps one StyleCacheEntry per variant (style_cache_layout_ /
// style_cache_paint_) rather than folding allow_pseudo into a single key, so a layout pass can
// never read paint's :hover/:pressed-flavored result or vice versa.
//
// A hit requires every input compute_style() actually reads to still match: class list, id,
// this element's own pseudo-state, the Stylesheet identity — pointer AND Stylesheet::generation,
// since some reload paths move-assign a freshly parsed Stylesheet into an already-engaged
// std::optional<Stylesheet> that keeps the same address, so pointer alone would miss a real
// content change — window size (media queries), this element's custom_properties (var()
// resolution), and the
// element's ancestor chain both by identity (descendant/child combinators) and by each ancestor's
// own pseudo-state (so "A:hover B" still reacts to A's hover changing even though the pointer
// chain to B is unchanged). `valid` starts false so the first call on a fresh/reconciled Element
// always misses and populates the cache.
//
// Deliberately NOT part of the cached style: animation. compute_style() itself never reads
// Element::animation_elapsed, so a cached ComputedStyle is always the pre-animation value —
// paint_element() applies apply_animation_opacity() on top of whatever compute_style() returns,
// cache hit or miss, every call, so animated opacity still advances every frame.
struct StyleCacheEntry {
    bool valid = false;
    ComputedStyle style{};
    std::vector<std::string> classes;
    std::string id;
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    bool focused = false;
    const Stylesheet* sheet = nullptr;
    std::uint64_t sheet_generation = 0;
    float window_width = 0.0f;
    float window_height = 0.0f;
    std::unordered_map<std::string, std::string> custom_properties;
    std::vector<const Element*> ancestors;
    // Packed (hovered|pressed<<1|disabled<<2|focused<<3) per ancestors[i], same order/length.
    std::vector<std::uint8_t> ancestor_pseudo_state;
};

struct Element {
    ElementKind kind = ElementKind::Canvas;
    std::string id;
    std::vector<std::string> classes;
    std::string name;

    std::string text;
    BindingId text_binding{};
    BindingId content_binding{};
    BindingId command_binding{};
    BindingId source_binding{};
    BindingId items_source_binding{};
    // {binding path} target for a `drag="{binding ...}"` attribute (any element kind, not just
    // Button) — writes a [0,1] fraction along the element's own rect while the pointer drags it
    // (canvas.cpp handle_pointer/update_drag). Unset (is_bound() false) means the element isn't a
    // drag target.
    BindingId drag_binding{};
    // {binding path} target for `paint="{binding ...}"` (any element kind). Unset means no custom
    // draw; `IPaint*` is filled in bind_element like `command`.
    BindingId paint_binding{};
    // Viewport camera. Unbound axes stay pan 0 / zoom 1; gestures that need a missing binding no-op.
    BindingId pan_x_binding{};
    BindingId pan_y_binding{};
    BindingId zoom_binding{};
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    float zoom = 1.0f;
    // Overflow and scrolling
    Overflow overflow_x = Overflow::Visible;
    Overflow overflow_y = Overflow::Visible;
    BindingId scroll_x_binding{};
    BindingId scroll_y_binding{};
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    float max_scroll_x = 0.0f;
    float max_scroll_y = 0.0f;
    std::optional<Length> scrollbar_width;
    glm::vec4 scrollbar_track_color{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 scrollbar_thumb_color{0.4f, 0.4f, 0.4f, 0.8f};
    glm::vec4 scrollbar_thumb_hover_color{0.6f, 0.6f, 0.6f, 1.0f};
    Length scrollbar_border_radius{4.0f, LengthUnit::Px};
    bool scrollbar_thumb_hovered = false;
    bool scrollbar_dragging = false;
    std::optional<AssetId> source;
    std::optional<LengthInsets> slice;
    std::vector<CustomPropertyBinding> custom_property_bindings;
    std::unordered_map<std::string, std::string> custom_properties;

    StackDirection direction = StackDirection::Vertical;
    // Which axis `drag_binding` reads pointer position along — independent of `direction` (a Stack
    // used as a drag track isn't necessarily laying its children out along the same axis).
    StackDirection drag_orientation = StackDirection::Horizontal;
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
    IPaint* paint = nullptr;
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    bool focused = false;
    std::size_t caret_position = 0;
    float caret_blink_timer = 0.0f;
    // Memoizes measure_element_text (document.cpp) across frames: when `text`/`font_family`/the
    // resolved `font_size` passed to IUiPainter::measure_text still match the last real-painter
    // measurement, layout reuses `text_measure_cache_result` instead of re-shaping glyphs. Populated
    // only on the `painter != nullptr` path — the `painter == nullptr` fallback (layout() overload
    // with no painter, e.g. tests) never reads or writes this cache, since it's a cheap
    // approximation that must not shadow a real measurement or vice versa. `mutable` so the cache
    // can be filled from a `const Element&` measuring context without widening every caller in the
    // layout call chain to non-const. For reconciled ItemsControl-generated items (generated_owner),
    // this cache survives frames along with the rest of the Element's runtime state, which is the
    // whole point: an unchanged row's text is measured once, not every frame.
    mutable bool text_measure_cache_valid = false;
    mutable std::string text_measure_cache_text;
    mutable AssetId text_measure_cache_font_family{};
    mutable float text_measure_cache_font_size = 0.0f;
    mutable glm::vec2 text_measure_cache_result{0.0f, 0.0f};

    // Memoizes compute_style() (paint.cpp), one slot per allow_pseudo variant — see
    // StyleCacheEntry's comment above for what invalidates a hit. `mutable` for the same reason as
    // the text-measure cache: compute_style() takes `const Element&`, and reconciled
    // ItemsControl-generated items (generated_owner) carry this cache across frames like the rest
    // of their runtime state, so an unchanged row's style is matched against the stylesheet once,
    // not every frame.
    mutable StyleCacheEntry style_cache_layout_;
    mutable StyleCacheEntry style_cache_paint_;

    std::vector<Element> children;
    std::vector<Element> generated_items;
    // Identity of the ViewModel* a generated_items entry was cloned for (opaque — never
    // dereferenced, only compared). Lets bind_element's ItemsControl reconciliation reuse the same
    // Element across frames for an item still in items_source, instead of rebuilding from the
    // static ItemTemplate every frame — which would otherwise reset animation_elapsed and any other
    // per-instance runtime state each frame. Unset (nullptr) on every non-generated Element.
    const void* generated_owner = nullptr;
    // True only for a synthetic spacer Element that ItemsControl virtualization (bind_element,
    // document.cpp) inserts before/after the visible window of generated rows, standing in for the
    // scrolled-past items' combined height so layout_stack's ordinary packing (unmodified) places
    // the real, visible rows at the correct Y and computes the correct max_scroll_y without knowing
    // virtualization exists. Its `height` is computed once in bind_element from row height *
    // skipped-row count — apply_layout_style (paint.cpp) checks this flag and skips resolving/
    // overwriting size-affecting style fields for such an element, since a class-less, id-less
    // Canvas has nothing for the stylesheet to say about its size and the unconditional
    // `element.height = style.height` it does for every other element would reset height to
    // nullopt. Never true for a real generated item, template, or hand-authored document Element.
    bool is_virtualization_spacer = false;
};

struct UiDocument {
    Element root;
    std::optional<AssetId> stylesheet;
};

struct UiInstance {
    UiDocument document;
    std::optional<Stylesheet> stylesheet;
    std::optional<AssetId> loaded_document;
    std::optional<AssetId> loaded_stylesheet;
    std::vector<AssetId> loaded_extra_stylesheets;
    std::vector<AssetId> loaded_sheet_ids;
    ViewModel* loaded_data_context = nullptr;
};

// Resolves an `<ItemTemplate src="...">` reference to the referenced file's raw XML text.
// All `src` values in a document tree are relative to that document's own file, regardless of
// include nesting depth — the resolver owns path composition. Returns nullopt if unreadable.
using UiIncludeResolver = std::function<std::optional<std::string>(std::string_view src)>;

[[nodiscard]] std::expected<UiDocument, UiError> parse_xml(std::string_view xml, IFatalError* fatal = nullptr,
        const ViewModel* data_context = nullptr, const UiIncludeResolver& resolve_include = {});

std::expected<void, UiError> apply_bindings(UiDocument& document, ViewModel& data_context, IFatalError* fatal = nullptr);

void layout(UiDocument& document, const render::Rect& canvas_rect);

[[nodiscard]] Element* find_by_kind(Element& root, ElementKind kind);
[[nodiscard]] const Element* find_by_kind(const Element& root, ElementKind kind);

// Finds the generated Element currently stamped with this exact Element::generated_owner value
// (see that field's comment) — used by canvas.cpp's drag write-back to re-resolve which item
// ViewModel a drag started inside an ItemsControl/ItemTemplate still belongs to, on every frame of
// the drag, rather than holding a ViewModel* across frames without revalidating it. `owner ==
// nullptr` always returns nullptr (no Element is ever a "generated" root with a null owner in a
// way that should match).
[[nodiscard]] Element* find_by_generated_owner(Element& root, const void* owner);

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

// Topmost interactive element under (x, y): prunes by hit_bounds() containment, visits siblings
// in reverse stacking order (highest z-index / last-drawn first), returns the first element that
// is a Button, or has a bound `command` or `drag` (any element kind), or a Viewport with a camera
// binding, or nullptr. Viewport camera inverses the pointer for descendants and clips to the
// unpanned layout_rect. Shared by click resolution (canvas.cpp) and hover resolution (paint.cpp).
[[nodiscard]] Element* hit_test(Element& root, float x, float y);

[[nodiscard]] inline bool has_viewport_camera(const Element& element) noexcept {
    return is_bound(element.pan_x_binding) || is_bound(element.pan_y_binding) || is_bound(element.zoom_binding);
}

[[nodiscard]] inline float viewport_zoom(float zoom) noexcept {
    return zoom > 0.0f ? zoom : 1.0f;
}

// Inverse of the Viewport paint camera: displayed = O + Z * (layout - O + P).
[[nodiscard]] inline glm::vec2 viewport_to_display(
        glm::vec2 origin, glm::vec2 pan, float zoom, glm::vec2 layout) noexcept {
    const float z = viewport_zoom(zoom);
    return origin + z * (layout - origin + pan);
}

[[nodiscard]] inline glm::vec2 inverse_viewport_pointer(const Element& viewport, glm::vec2 pointer) noexcept {
    const glm::vec2 origin{viewport.layout_rect.x, viewport.layout_rect.y};
    const float z = viewport_zoom(viewport.zoom);
    return origin + (pointer - origin) / z - glm::vec2{viewport.pan_x, viewport.pan_y};
}

// Pan that keeps `pointer` (display/layout space) on the same content point after zoom changes.
[[nodiscard]] inline glm::vec2 viewport_pan_after_zoom(
        glm::vec2 origin, glm::vec2 pan, float zoom, float new_zoom, glm::vec2 pointer) noexcept {
    const float z = viewport_zoom(zoom);
    const float nz = viewport_zoom(new_zoom);
    return (pointer - origin) * (1.0f / nz - 1.0f / z) + pan;
}

// Innermost Viewport whose clip contains `pointer` after ancestor camera inverses. Used by wheel
// zoom so a node under the cursor still zooms its enclosing Viewport.
[[nodiscard]] Element* find_viewport_at(Element& root, float x, float y);

[[nodiscard]] inline bool is_scrollable_y(const Element& element) noexcept {
    return element.overflow_y == Overflow::Scroll ||
            (element.overflow_y == Overflow::Auto && element.max_scroll_y > 0.0f);
}

[[nodiscard]] inline bool is_scrollable_x(const Element& element) noexcept {
    return element.overflow_x == Overflow::Scroll ||
            (element.overflow_x == Overflow::Auto && element.max_scroll_x > 0.0f);
}

[[nodiscard]] inline bool is_scrollable(const Element& element) noexcept {
    return is_scrollable_y(element) || is_scrollable_x(element);
}

[[nodiscard]] render::Rect scrollbar_track_rect(const Element& element, float ui_scale = 1.0f) noexcept;
[[nodiscard]] render::Rect scrollbar_thumb_rect(const Element& element, float ui_scale = 1.0f) noexcept;

// Innermost scrollable container whose clip contains `pointer`. Used by wheel scrolling.
[[nodiscard]] Element* find_scrollable_at(Element& root, float x, float y);

}
