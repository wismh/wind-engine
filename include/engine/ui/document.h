#pragma once

// docs/tech/features/UI Markup.md

#include <engine/builtin_ids.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/cursor.h>
#include <engine/ui/element_arena.h>
#include <engine/ui/element_id.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/text_line.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::loc {
    class Catalog;
}

namespace engine::ui {

    enum class UiError {
        InvalidMarkup,
        UnknownElement,
        MissingBinding,
        MissingString,
        ForbiddenContent,
        Io,
        CyclicInclude,
    };

    // One `{tr key name={binding path}}` argument. `binding` is interned like any other `{binding}`.
    struct TrArg {
        std::string name;
        BindingId binding{};
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
        Checkbox,
        Math,
        Popup,
        Slider,
    };

    // Kinds that pack their children along `direction` with `gap`, vertical by default. A Popup is a menu-like
    // column of its items.
    [[nodiscard]] constexpr bool packs_children(ElementKind kind) noexcept {
        return kind == ElementKind::Stack || kind == ElementKind::ScrollView || kind == ElementKind::Popup;
    }

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

    // `white-space` for Label/Button text. Normal wraps at the content width; NoWrap keeps one line.
    enum class WhiteSpace {
        Normal,
        NoWrap,
    };

    // `user-select` for a Label. None (the default) leaves the label out of hit-testing. Text is a normal
    // selection (drag, double-click word, triple-click all). All selects the whole string on one click.
    // Not inherited — same as every other property here. Buttons ignore it.
    enum class UserSelect {
        None,
        Text,
        All,
    };

    enum class PositionMode {
        Static,
        Relative,
        Absolute,
    };

    // Where a Popup sits against its anchor (its parent's border box): the side first, then which edge of
    // that side it lines up with. A popup that does not fit on that side flips to the opposite one when that
    // side has more room, and is then pushed inside the window.
    enum class PopupPlacement {
        BottomStart,
        BottomEnd,
        TopStart,
        TopEnd,
        RightStart,
        RightEnd,
        LeftStart,
        LeftEnd,
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

    // `line-height` for a Label/Button whose text breaks into rows. Normal keeps the font's own line
    // height. Factor is a unitless multiplier of the used font-size (a bare `1.5` is that multiplier).
    // Length is px, em, or %; em and % both resolve against this element's font-size.
    enum class LineHeightKind {
        Normal,
        Factor,
        Length,
    };

    struct LineHeight {
        LineHeightKind kind = LineHeightKind::Normal;
        float factor = 0.0f;
        Length length{};
    };

    constexpr float kDefaultFontSize = 16.0f;
    constexpr float kViewportMinZoom = 0.25f;
    constexpr float kViewportMaxZoom = 4.0f;
    constexpr float kViewportZoomStep = 1.1f;

    [[nodiscard]] inline float resolve_literal(float value, LengthUnit unit, float percent_basis,
                                               float em_basis) noexcept {
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

    [[nodiscard]] inline float resolve_calc_node(const std::vector<CalcNode> &nodes, std::size_t index,
                                                 float percent_basis, float em_basis) noexcept {
        if (index >= nodes.size()) {
            return 0.0f;
        }
        const CalcNode &node = nodes[index];
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

    [[nodiscard]] inline float resolve_length(const Length &length, float percent_basis, float em_basis) noexcept {
        if (!length.calc.empty()) {
            return resolve_calc_node(length.calc, length.calc.size() - 1, percent_basis, em_basis);
        }
        return resolve_literal(length.value, length.unit, percent_basis, em_basis);
    }

    [[nodiscard]] inline float resolve_font_size(const Length &font_size, float percent_basis) noexcept {
        if (font_size.calc.empty() && font_size.unit == LengthUnit::Em) {
            return font_size.value * kDefaultFontSize;
        }
        return resolve_length(font_size, percent_basis, kDefaultFontSize);
    }

    // `metric` is the font's own line height, used when `line_height` is normal. A length's em and %
    // both resolve against `font_size` (a line-height percentage is of the font-size).
    [[nodiscard]] inline float resolve_line_height(const LineHeight &line_height, float font_size,
                                                   float metric) noexcept {
        switch (line_height.kind) {
            case LineHeightKind::Normal:
                return metric;
            case LineHeightKind::Factor:
                return line_height.factor * font_size;
            case LineHeightKind::Length:
                return resolve_length(line_height.length, font_size, font_size);
        }
        return metric;
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

    enum class GradientKind {
        Linear,
        Radial,
        Conic,
    };

    // One color-stop in a Gradient. `percent` (0-100) is nullopt when the stylesheet omitted it —
    // resolved to an even spread across the stop list at parse time (see parse_gradient, paint.cpp),
    // not at paint time, so a painter only ever sees fully-resolved percentages.
    struct GradientStop {
        glm::vec4 color{};
        std::optional<float> percent;
    };

    // `background`/`background-image: linear-gradient()|radial-gradient()|conic-gradient()`. `angle_deg`
    // only applies to Linear (CSS convention: 0deg = to top, clockwise). Conic has no native NanoVG
    // paint (nvgImagePattern is affine-only) and is rendered by baking a small texture cached by this
    // struct's content — see IUiPainter::fill_rounded_rect_gradient / NanoVgPainter. That makes it a
    // fit for a background that changes rarely (a handful of stylesheet classes/pseudo-states), not for
    // a value animated every frame — that would rebake a GPU texture every frame. A live,
    // continuously-animated ring should instead be drawn imperatively through IPaint/IDrawList
    // (paint.h/draw_list.h), not by swapping this value's stops every frame.
    struct Gradient {
        GradientKind kind = GradientKind::Linear;
        float angle_deg = 180.0f;
        std::vector<GradientStop> stops;
    };

    // Default is linear, not the web's `ease`: existing opacity keyframes (splash included) are
    // authored against a linear clock, and `ease` would move the halfway sample off 0.5.
    enum class CssEasing : std::uint8_t {
        Linear,
        Ease,
        EaseIn,
        EaseOut,
        EaseInOut,
    };

    // Properties `transition` and `@keyframes` can drive. `Overflow` / `ScrollbarColor` are the
    // shorthands; `all` expands to the longhands and does not include those two.
    enum class MotionProp : std::uint8_t {
        Color,
        Background,
        Opacity,
        Visibility,
        Display,
        Width,
        Height,
        MinWidth,
        MaxWidth,
        MinHeight,
        Padding,
        Margin,
        Gap,
        FlexDirection,
        AlignItems,
        JustifyContent,
        TextAlign,
        WhiteSpace,
        UserSelect,
        BackgroundImage,
        BackgroundSlice,
        BackgroundRepeat,
        BorderRadius,
        BorderWidth,
        BorderColor,
        FontSize,
        LineHeight,
        FontFamily,
        ZIndex,
        Position,
        Top,
        Right,
        Bottom,
        Left,
        Transform,
        X1,
        Y1,
        X2,
        Y2,
        Stroke,
        StrokeWidth,
        Overflow,
        OverflowX,
        OverflowY,
        ScrollbarWidth,
        ScrollbarColor,
        ScrollbarThumbColor,
        ScrollbarTrackColor,
        ScrollbarThumbHoverColor,
        ScrollbarBorderRadius,
        SelectionColor,
        Count,
    };

    // One captured property value. Lengths keep their specified form (for retarget equality) and the
    // px they resolved to against the basis of the frame that read them (`px` / `px_r` / `px_b` / `px_l`).
    struct MotionValue {
        bool present = false;
        float number = 0.0f;
        float number2 = 0.0f;
        int integer = 0;
        bool flag = false;
        bool flag2 = false;
        glm::vec4 color{};
        glm::vec4 color2{};
        Length length{};
        std::optional<Length> opt_length{};
        LengthInsets insets{};
        LineHeight line_height{};
        AssetId asset{};
        std::optional<AssetId> image{};
        std::optional<Gradient> gradient{};
        bool has_gradient = false;
        std::uint8_t enumer = 0;
        std::uint8_t enumer2 = 0;
        float px = 0.0f;
        float px_r = 0.0f;
        float px_b = 0.0f;
        float px_l = 0.0f;
    };

    // Parallel to one `transition-property` entry. `running` is false on the frame the property was
    // first observed and again once the clock has reached the end; `shown` is what paint and layout read.
    struct TransitionRuntime {
        MotionProp prop = MotionProp::Opacity;
        float elapsed = 0.0f;
        float duration = 0.0f;
        float delay = 0.0f;
        CssEasing easing = CssEasing::Linear;
        bool seen = false;
        bool running = false;
        MotionValue from{};
        MotionValue to{};
        MotionValue shown{};
    };

    // One `@keyframes` clock. `iterations < 0` is `infinite`. Elapsed survives ItemsControl reconcile
    // because the whole struct lives on the reused Element.
    struct AnimationRuntime {
        std::string name;
        float elapsed = 0.0f;
        float duration = 0.0f;
        float delay = 0.0f;
        CssEasing easing = CssEasing::Linear;
        float iterations = 1.0f;
    };

    struct ShownMotion {
        MotionProp prop = MotionProp::Opacity;
        MotionValue value{};
        bool running = false;
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
        // Alternate value for the same background-image layer as `background_image` — a stylesheet
        // sets exactly one of the two (parse_gradient tried first in apply_declaration, paint.cpp).
        std::optional<Gradient> background_gradient;
        std::optional<LengthInsets> background_slice;
        BackgroundRepeat background_repeat = BackgroundRepeat::NoRepeat;
        float opacity = 1.0f;
        bool visible = true;
        // `display: none`: the element and its subtree take no part in layout, paint or hit-testing.
        bool display_none = false;
        Length gap{};
        bool has_gap = false;
        StackDirection direction = StackDirection::Vertical;
        bool has_direction = false;
        LengthInsets padding{};
        LengthInsets margin{};
        std::optional<Length> width;
        std::optional<Length> height;
        std::optional<Length> min_width;
        std::optional<Length> max_width;
        std::optional<Length> min_height;
        UiAlign justify = UiAlign::Start;
        UiAlign align_items = UiAlign::Start;
        UiAlign text_align = UiAlign::Start;
        WhiteSpace white_space = WhiteSpace::Normal;
        UserSelect user_select = UserSelect::None;
        // `repaint-boundary`: the element keeps its own picture (see Element::picture).
        bool repaint_boundary = false;
        Cursor cursor = Cursor::Auto;
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
        LineHeight line_height{};
        AssetId font_family = builtin::font_ui;
        // Parallel lists. One duration/delay/easing/iteration applies to every name or property; a
        // shorter list repeats its last value; extras past the name/property count are ignored.
        // `animation_iterations[i] < 0` means `infinite`.
        std::vector<std::string> transition_properties;
        std::vector<float> transition_durations;
        std::vector<float> transition_delays;
        std::vector<CssEasing> transition_easings;
        std::vector<std::string> animation_names;
        std::vector<float> animation_durations;
        std::vector<float> animation_delays;
        std::vector<CssEasing> animation_easings;
        std::vector<float> animation_iterations;
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
        // TextInput-only: highlight painted behind selected text (paint.cpp), not a scrollbar
        // property — grouped with the other optionally-cascaded colors above for consistency.
        glm::vec4 selection_color{0.2f, 0.4f, 0.9f, 0.35f};
        bool has_selection_color = false;
        // Cascaded `--name: value;` declarations, keyed without the leading `--`. Consulted by
        // resolve_var() when a declaration's value is `var(--name)` and the element itself has no
        // matching entry in Element::custom_properties (per-instance, VM-bound — takes priority).
        std::unordered_map<std::string, std::string> custom_properties;
    };

    struct Element;

    // Memoizes one compute_style() call (paint.cpp) for one Element + one allow_pseudo variant.
    // apply_layout_style() always calls with allow_pseudo=false. paint_element() and the motion walk
    // call with allow_pseudo=true — Element keeps one StyleCacheEntry per variant (style_cache_layout_ /
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
    // Deliberately NOT part of the cached style: transition and @keyframes. compute_style() never
    // reads Element motion clocks, so a cached ComputedStyle is the pre-motion cascade. advance_motion()
    // samples on top of that style when the cache misses or a clock still needs time. A quiet hit
    // leaves the previous sample in place.
    class Picture;

    // What a picture was recorded for. A picture is replayed only while every part of this still holds.
    struct PictureKey {
        render::Rect rect{};
        glm::vec2 offset{};
        float scale = 1.0f;
        glm::vec2 parent_content{};
        float window_width = 0.0f;
        float window_height = 0.0f;
        const Stylesheet *sheet = nullptr;
        std::uint64_t sheet_generation = 0;
        // Set when the children were recorded with the element's own state (opacity, transform) set again around them:
        // that state is then part of what they show.
        float baked_opacity = 0.0f;
        float baked_rotation = 0.0f;
        float baked_scale = 0.0f;

        bool operator==(const PictureKey &) const = default;
    };

    // An `IPaint` an element's picture drew, and the version it had then.
    struct PictureWatch {
        const IPaint *paint = nullptr;
        std::uint64_t version = 0;
    };

    // One recorded picture of a boundary and what it depends on besides its key.
    struct PicturePart {
        std::shared_ptr<Picture> picture;
        bool valid = false;
        // Recorded something that changes without a mark (a focused text input's caret, an `IPaint` with no version):
        // never replayed.
        bool volatile_part = false;
        std::vector<PictureWatch> watches;
        // A `Viewport`'s content: the part of the world (layout space) its children were recorded for. The picture is
        // replayed while the camera shows nothing outside it.
        render::Rect world{};
    };

    // The retained paint of a repaint boundary: its own drawing (`chrome`) and its children (`content`), both recorded
    // in screen coordinates under the state the element sets itself (opacity, transform, scissor, camera), which is
    // applied live and so is a parameter of the picture, not part of it. A copy of an element starts with no picture.
    struct PictureSlot {
        PictureKey key{};
        // The state the element sets itself (opacity, transform, scissor, scroll or camera), the calls of `chrome` and
        // `content` between them, and the scrollbar: written again whenever paint reaches the element, a few commands.
        std::shared_ptr<Picture> frame;
        PicturePart chrome;
        PicturePart content;

        PictureSlot() = default;
        PictureSlot(const PictureSlot &) {}
        PictureSlot &operator=(const PictureSlot &other) {
            if (this != &other) {
                *this = PictureSlot{};
            }
            return *this;
        }
        PictureSlot(PictureSlot &&) noexcept = default;
        PictureSlot &operator=(PictureSlot &&) noexcept = default;
    };

    struct StyleCacheEntry {
        bool valid = false;
        ComputedStyle style{};
        std::vector<std::string> classes;
        std::string id;
        // The element's own state, packed like `kPseudo*` (hover, pressed, disabled, focus, checked), as it was when
        // the cascade ran. A hit compares it only through `self_dep`.
        std::uint8_t pseudo = 0;
        const Stylesheet *sheet = nullptr;
        std::uint64_t sheet_generation = 0;
        float window_width = 0.0f;
        float window_height = 0.0f;
        std::unordered_map<std::string, std::string> custom_properties;
        // The ancestor chain by element id (descendant and child combinators match through it). An ancestor with no
        // id (a tree built by hand and never indexed) never matches, so such a tree misses and restyles.
        std::vector<ElementId> ancestors;
        // The same packing per ancestors[i], same order/length.
        std::vector<std::uint8_t> ancestor_pseudo_state;
        // Which of those bits the cascade actually read: `self_dep` for the element's own state, `ancestor_dep[i]` for
        // ancestors[i]. A rule with a pseudo-class on a compound that the element (or that ancestor) matches
        // structurally depends on that bit. A hit compares the state only through these masks, so hovering a
        // container restyles only the descendants a rule like `A:hover B` can match.
        std::uint8_t self_dep = 0;
        std::vector<std::uint8_t> ancestor_dep;
    };

    struct Element {
        ElementKind kind = ElementKind::Canvas;
        // The element's identity in its document's arena (`UiDocument::find`) and its parent's. Null on an element no
        // `UiDocument::reindex` has reached (a tree built by hand). Written by the engine: parse, `reindex`, and the
        // ItemsControl reconcile. `generated_owner` stays the identity of the data a row shows; this is the
        // identity of the element, and a row that is reused keeps it.
        ElementId handle{};
        ElementId parent{};
        std::string id;
        std::vector<std::string> classes;
        std::string name;

        std::string text;
        BindingId text_binding{};
        // `{tr key}` or `{tr key name={binding path}}` on `text` / `content`. apply_bindings writes the
        // resolved message into `text`. Empty means this element is not a translation.
        std::string tr_key;
        std::vector<TrArg> tr_args;
        BindingId content_binding{};
        BindingId command_binding{};
        // {binding path} target for a `checked="{binding ...}"` attribute (Checkbox only) — two-way:
        // bind_element reads it into `checked` every frame like `text`/`content`, and canvas.cpp's
        // click/Enter handling writes the toggled value back through write_property_float (bool rides
        // the arithmetic float path, same as every other bool ViewModel property). A literal
        // `checked="true"` with no binding just seeds `checked` once at parse time.
        BindingId checked_binding{};
        BindingId source_binding{};
        // {binding path} target for `if="{binding ...}"` (any element kind), a bool. While it reads false the
        // element is out of layout, paint, and hit-testing like `display: none` (bind_element sets
        // `hidden_by_if`; apply_layout_style and paint_element read it). Unset keeps the element as it is.
        BindingId if_binding{};
        bool hidden_by_if = false;
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
        glm::vec4 selection_color{0.2f, 0.4f, 0.9f, 0.35f};
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
        // `<Slider value="{binding v}" min max step/>`: the value binding is `drag_binding`, `drag_orientation` picks
        // the axis, and these are the range. `step` 0 is continuous. The two parts, `children[0]` (class
        // `slider-fill`) and `children[1]` (class `slider-handle`), are generated at parse time; bind sets the custom
        // property `--fraction` (0..1, from the value) on the slider and on both parts for the stylesheet to place them.
        float slider_min = 0.0f;
        float slider_max = 1.0f;
        float slider_step = 0.0f;
        Length gap{};
        LengthInsets padding{};
        LengthInsets margin{};
        std::optional<Length> width;
        std::optional<Length> height;
        std::optional<Length> min_width;
        std::optional<Length> max_width;
        std::optional<Length> min_height;
        UiAlign justify = UiAlign::Start;
        UiAlign align_items = UiAlign::Start;
        UiAlign text_align = UiAlign::Start;
        WhiteSpace white_space = WhiteSpace::Normal;
        UserSelect user_select = UserSelect::None;
        // Pseudo-less `cursor`, like the other layout copies. cursor_at() resolves Auto.
        Cursor cursor = Cursor::Auto;
        Length font_size{kDefaultFontSize, LengthUnit::Px};
        LineHeight line_height{};
        AssetId font_family{};
        // Motion clocks. Reconcile moves the Element, so these survive an ItemsControl row reuse.
        // advance_motion() rebuilds `motion_shown` when the paint style cache misses, a clock is still
        // running (animation delay included), or a held sample's parent basis changed. A quiet frame
        // leaves the vectors alone. `layout_inputs_changed` is set when the sample moved a layout field: the paint pass
        // then marks the element for restyle and layout.
        std::vector<TransitionRuntime> transition_players;
        std::vector<AnimationRuntime> animation_players;
        std::vector<ShownMotion> motion_shown;
        // Parent-content basis of the last sample that left `motion_shown` non-empty. A quiet frame
        // with an empty `motion_shown` does not consult it; a held percentage keyframe does, so a
        // parent resize re-resolves the pixels.
        glm::vec2 motion_sample_basis{};
        bool motion_sample_basis_valid = false;
        bool layout_inputs_changed = false;
        // True while a row-box property (height, padding, gap, font-size, ...) is still interpolating.
        // ItemsControl turns that into suppress_item_virtualization for the following bind.
        bool height_motion_active = false;
        // A `Viewport` only: how many children the last paint skipped because they lie wholly outside the camera.
        mutable std::uint32_t culled_children = 0;
        // The element keeps its own picture (`repaint-boundary`, written by the layout style). A scroll container, a
        // `Viewport`, a `Popup` and the root are boundaries whatever this says.
        bool repaint_boundary = false;
        // The rect this element had when paint last looked at it: a layout pass that moved it marks it for paint.
        render::Rect rect_seen{};
        PictureSlot picture;
        bool suppress_item_virtualization = false;
        glm::vec2 layout_used_cache{};
        bool layout_used_cache_valid = false;
        float layout_used_basis_w = 0.0f;
        float layout_used_basis_h = 0.0f;
        float layout_used_avail_x = 0.0f;
        int z_index = 0;
        PositionMode position = PositionMode::Static;
        std::optional<Length> inset_top;
        std::optional<Length> inset_right;
        std::optional<Length> inset_bottom;
        std::optional<Length> inset_left;
        float rotation_deg = 0.0f;
        float scale = 1.0f;
        // Pseudo-less copies of ComputedStyle::visible / display_none (apply_layout_style), read by layout and
        // hit-testing. `visibility: hidden` keeps the element's space; `display: none` removes it from layout.
        bool visible = true;
        bool display_none = false;

        render::Rect layout_rect{};
        ICommand *command = nullptr;
        IPaint *paint = nullptr;
        bool hovered = false;
        bool pressed = false;
        // Bound command sets this from !can_execute() on every bind, except TextInput (Enter-to-submit
        // must stay typeable when the command is not yet executable). :disabled follows this flag.
        bool disabled = false;
        bool focused = false;
        // Checkbox-only runtime state, toggled by canvas.cpp on click/Enter and matched by the CSS
        // `:checked` pseudo-class (paint.cpp subject_matches) — the checked/unchecked look itself is
        // ordinary cascaded background/border/background-image, not a built-in drawn mark, same as
        // Button carries no built-in chrome of its own.
        bool checked = false;
        // Popup-only. `open` is a literal or a two-way binding: a click outside, Escape, or a wheel outside
        // closes the popup and writes false back. A closed popup is laid out but not painted or hit.
        bool open = false;
        BindingId open_binding{};
        PopupPlacement placement = PopupPlacement::BottomStart;
        // Layout units from where layout left the popup (its anchor's top-left) to where it is shown, after
        // the anchor's scroll and Viewport camera, the flip, and the clamp into the window. Written by the
        // canvas before hit-testing and painting; every walk that enters an open popup starts from it.
        glm::vec2 popup_offset{};
        std::size_t caret_position = 0;
        // IME preedit, kept off `text` so apply_bindings can refresh `text` from the ViewModel every
        // frame and leave the preedit in place. composition_start and composition_length are Unicode
        // code points into composition (SDL_TextEditingEvent); -1 means unset.
        std::string composition;
        int composition_start = -1;
        int composition_length = -1;
        float caret_blink_timer = 0.0f;
        // TextInput and selectable Label: the fixed end of an in-progress selection; caret_position is
        // the live end. unset = no selection. A real (non-collapsed) selection is
        // selection_anchor.has_value() && *selection_anchor != caret_position, spanning
        // [min(*selection_anchor, caret_position), max(...)). Set by Ctrl+A (0..text.size()),
        // pointer-down (click == drag-select start), and Shift+Left/Right/Home/End (armed from the
        // caret before it moves). Any unmodified caret move/edit or focus loss clears it (canvas.cpp).
        std::optional<std::size_t> selection_anchor;
        // TextInput, and a selectable Label for copy: per-field clipboard lock, XML `allow-copy`/
        // `allow-paste` (literal only, no {binding} — a static field capability, not runtime-toggled
        // state like `checked`). Mirrors the web's per-event copy/cut/paste interception: independent
        // flags, not one on/off switch. Cut is gated by allow_copy (it reads before deleting), not
        // allow_paste. A Label never cuts or pastes; allow_copy still blocks Ctrl+C.
        bool allow_copy = true;
        bool allow_paste = true;
        // TextInput, and a selectable Label: real-screen-pixel text metrics as last painted. TextInput
        // stores the resolved font size and the text's x-origin (paint.cpp). A Label stores the font
        // size plus one PaintedTextLine per row (glyph left/top, not the text-align anchor).
        // canvas.cpp's click-to-index reads these back instead of re-resolving CSS length units itself,
        // so a click can never land somewhere paint.cpp would have drawn the text differently. 0 / empty
        // until the element has painted at least once — click handling then falls back to the end of
        // the text.
        float painted_font_size_px = 0.0f;
        float painted_content_origin_x = 0.0f;
        std::vector<PaintedTextLine> painted_text_lines;
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
        // Same idea for a wrapped Label/Button (`white-space: normal`, document.cpp's measure_element_text): the rows
        // `text` broke into at `text_wrap_cache_width` (content-box width, design pixels), and the size they make up.
        // Keyed separately from the single-line cache above because a hug pass and the final pass of one layout()
        // measure the same element at different widths; sharing one key would make every frame miss. Only filled for
        // text that actually needs breaking (wider than the width, or containing a newline); never by the painter-less
        // fallback. `text_wrap_cache_line_height` is part of the key: -1 is `line-height: normal` (the font metric
        // stays on `text_wrap_cache_block.line_height`); any other value is the explicit stride in design px that
        // `text_wrap_cache_result.y` was measured with.
        mutable bool text_wrap_cache_valid = false;
        mutable std::string text_wrap_cache_text;
        mutable AssetId text_wrap_cache_font_family{};
        mutable float text_wrap_cache_font_size = 0.0f;
        mutable float text_wrap_cache_line_height = -1.0f;
        mutable float text_wrap_cache_width = 0.0f;
        mutable TextBlock text_wrap_cache_block;
        mutable glm::vec2 text_wrap_cache_result{0.0f, 0.0f};

        // Math only: `display="true"` asks for display style (larger fractions, operator limits above and below);
        // false is text style. The formula source lives in `text` (`formula="..."` or `formula="{binding}"`), so
        // the layout dirty-gate that already compares `text` covers it.
        bool math_display = false;
        // Math only: the laid-out formula, keyed by (text, font size, display, font). Opaque because the concrete
        // type lives in the private src/ui/math code — a public header cannot name it. A `shared_ptr<void>` keeps
        // the right deleter; a cache entry is never mutated, only replaced, so Elements cloned from one template
        // (which copy this pointer) can share it safely.
        mutable std::shared_ptr<void> math_cache;
        // Label/Button text that contains inline `\(...\)`: the laid-out runs. Opaque, like `math_cache`,
        // because the concrete type lives under src/ui. Replaced, never mutated, so an Element cloned from
        // a template can share the pointer until its own text is measured.
        mutable std::shared_ptr<void> inline_cache;

        // Memoizes compute_style() (paint.cpp), one slot per allow_pseudo variant — see
        // StyleCacheEntry's comment above for what invalidates a hit. `mutable` for the same reason as
        // the text-measure cache: compute_style() takes `const Element&`, and reconciled
        // ItemsControl-generated items (generated_owner) carry this cache across frames like the rest
        // of their runtime state, so an unchanged row's style is matched against the stylesheet once,
        // not every frame.
        mutable StyleCacheEntry style_cache_layout_;
        mutable StyleCacheEntry style_cache_paint_;

        // Persists the last known real (non-spacer) row height for ItemsControl virtualization
        // (bind_element, document.cpp) across frames, independent of whether last frame's
        // generated_items happened to contain a samplable row. A per-frame sample alone is not
        // enough once a wrapping ScrollView can scroll this ItemsControl entirely out of view: the
        // visible window then holds zero real rows (one spacer standing in for all of them), so
        // there is nothing to sample, yet row height hasn't actually become unknown. Without this
        // cache, several such frames in a row would each fail eligibility and fall back to full
        // generation, defeating virtualization for exactly the scrolled-off-screen case this exists
        // to cover. `mutable` for the same reason as the caches above: read from a `const Element&`,
        // and it survives frames on a generated_owner Element like the rest of its runtime state.
        mutable std::optional<float> virtualization_row_height_cache;
        // An ItemsControl whose rows have no fixed pixel height: the height each row last measured, by row owner (the
        // item's view-model, `generated_owner`), and the running prefix of tops built from them. A row not measured yet
        // counts as the mean of the measured ones.
        struct RowHeights {
            std::unordered_map<const void *, float> measured;
            double measured_sum = 0.0;
            // Goes up whenever a measured height changed or was added, so the prefix is built again.
            std::uint64_t epoch = 1;
            std::vector<const void *> items;
            std::vector<float> tops;
            std::uint64_t tops_epoch = 0;
            float tops_gap = 0.0f;
        };
        std::optional<RowHeights> row_heights;

        std::vector<Element> children;
        std::vector<Element> generated_items;
        // Identity of the ViewModel* a generated_items entry was cloned for (opaque — never
        // dereferenced, only compared). Lets bind_element's ItemsControl reconciliation reuse the same
        // Element across frames for an item still in items_source, instead of rebuilding from the
        // static ItemTemplate every frame — which would otherwise reset motion clocks and any other
        // per-instance runtime state each frame. Unset (nullptr) on every non-generated Element.
        const void *generated_owner = nullptr;
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

        // Dirty state (wind-228). `dirty_self` is what this element's own state needs again, `dirty_below` what some
        // descendant needs; each holds `kDirtyStyle`, `kDirtyLayout`, and `kDirtyPaint` bits (stylesheet.h). The bind
        // pass, a pseudo-class change, and the other sources of change set them through `mark_dirty` (src/ui/dirty.h),
        // which walks up by `parent` so a pass can find the dirty element from the root without visiting the rest, and
        // the pass that consumes a bit clears it. Layout stops marking at a relayout boundary. A document built by
        // hand is all dirty until its first pass. Motion keeps its own `layout_inputs_changed` pair above.
        std::uint8_t dirty_self = 0;
        std::uint8_t dirty_below = 0;
        // Diagnostic counters, never reset by the engine: how many times this element's cascade was computed (the
        // layout style and the paint style each count) and how many times layout placed it (a skipped subtree does
        // not count). Tests assert a quiet frame leaves them alone.
        mutable std::uint32_t style_rebuilds = 0;
        std::uint32_t layout_runs = 0;
        // What `layout_element` was last given for this element: the parent's content size and the containing block of
        // its absolutely positioned descendants. A relayout boundary is laid out again from these without its parent.
        glm::vec2 layout_parent_content{};
        render::Rect layout_containing_block{};
        // The element was `display: none` (or hidden by `if`) when it was last restyled, so layout has not visited its
        // subtree since: the marks inside it were consumed unseen, and the rects below are stale. The next layout that
        // reaches the element lays its whole subtree out again.
        bool layout_hidden = false;
    };

    // Slider arithmetic, shared by the bind pass (value to `--fraction`) and the pointer drag (position to value).
    // `value` is clamped into [slider_min, slider_max]; a range with no width is 0.
    [[nodiscard]] inline float slider_fraction(const Element &slider, float value) noexcept {
        const float span = slider.slider_max - slider.slider_min;
        if (!(span > 0.0f)) {
            return 0.0f;
        }
        const float t = (value - slider.slider_min) / span;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    // The nearest multiple of `slider_step` from `slider_min`, clamped to the range; the value unchanged (but clamped)
    // for a continuous slider.
    [[nodiscard]] inline float slider_snap(const Element &slider, float value) noexcept {
        if (slider.slider_step > 0.0f) {
            const float steps = std::floor((value - slider.slider_min) / slider.slider_step + 0.5f);
            value = slider.slider_min + steps * slider.slider_step;
        }
        if (value < slider.slider_min) {
            return slider.slider_min;
        }
        return value > slider.slider_max ? slider.slider_max : value;
    }

    // Gives a Slider its two parts: `children[0]` is the fill (class `slider-fill`) and `children[1]` the handle
    // (class `slider-handle`), both plain Stacks styled by the stylesheet. The XML parser and `ui::slider()` call it.
    void add_slider_parts(Element &slider);

    // The field of an element a binding slot feeds.
    enum class BindField : std::uint8_t {
        Text,
        Content,
        Source,
        If,
        SliderValue,
        PanX,
        PanY,
        Zoom,
        ScrollX,
        ScrollY,
        Checked,
        Open,
        Var,
        TrArg,
        ItemsSource,
        Command,
        Paint,
    };

    // One binding of one element: the version of the view-model property it read last. A quiet frame compares
    // `version.read()` with `seen` for every slot and does nothing else (a `Command` slot has no version, it polls
    // `can_execute()`, and a `Paint` slot reads nothing).
    struct BindSlot {
        ElementId element{};
        BindField field = BindField::Text;
        // The `Var` or `TrArg` entry of the element.
        std::uint16_t index = 0;
        BindingId binding{};
        // The view-model the slot reads: the canvas's data context, or the row's item inside an ItemsControl.
        ViewModel *source = nullptr;
        std::optional<ViewModel::PropertyVersion> version;
        std::uint64_t seen = 0;
        // A `Command` slot: what `can_execute()` said last.
        bool executable = true;
    };

    // A binding the build could not satisfy, kept so every pass reports it again like a full walk did.
    struct BindFailure {
        UiError error = UiError::MissingBinding;
        std::string message;
    };

    struct BindTable {
        std::vector<BindSlot> slots;
        std::vector<BindFailure> failures;
        // What the table was built against. Another data context or catalog, or a changed catalog version, builds it
        // again; so does a change of the tree's structure.
        const ViewModel *context = nullptr;
        const void *catalog = nullptr;
        std::uint64_t catalog_version = 0;
        bool built = false;
    };

    // Work the passes did on one document, never reset by the engine. Tests assert a quiet frame adds nothing.
    struct UiWorkCounters {
        // Binding slots compared with their view-model's version, and property reads a slot or a rebuild made.
        std::uint64_t slots_checked = 0;
        std::uint64_t bound_reads = 0;
        // Times the bind table was built again, layout passes that ran, and style passes that restyled elements.
        std::uint64_t table_builds = 0;
        std::uint64_t layout_passes = 0;
        std::uint64_t restyle_passes = 0;
        // The debug layout check (builds with `ENGINE_UI_LAYOUT_CHECK`): times it laid a copy out from scratch, and
        // times a rect of the live tree differed. A correct engine leaves the second at 0.
        std::uint64_t layout_checks = 0;
        std::uint64_t layout_check_failures = 0;
        // Paint: elements `paint_element` drew (a replayed boundary draws none of the elements under it), pictures
        // recorded and replayed, and the commands a replay issued.
        std::uint64_t painted_elements = 0;
        std::uint64_t pictures_recorded = 0;
        std::uint64_t pictures_replayed = 0;
        std::uint64_t picture_commands = 0;
        // The debug picture check: boundaries compared with a fresh recording, and differences found.
        std::uint64_t picture_checks = 0;
        std::uint64_t picture_check_failures = 0;
    };

    // What the painter measured and broke, shared by every element of one document: two rows with the same text, font,
    // size (and width) shape once. The per-element caches stay in front of it; this is what a recycled or cloned row
    // finds when its own cache is empty (`src/ui/text_cache.h`). It is dropped when the painter changes and bounded.
    struct SharedTextCache {
        const void *painter = nullptr;
        std::unordered_map<std::string, glm::vec2> measures;
        std::unordered_map<std::string, TextBlock> blocks;
        // Calls that reached the painter, and lookups the cache answered.
        std::uint64_t painter_measures = 0;
        std::uint64_t painter_breaks = 0;
        std::uint64_t hits = 0;
    };

    struct UiDocument {
        Element root;
        // The slot table behind every `Element::handle` of this document. Parse and `make_document` fill it.
        ElementArena arena;
        // Every binding of the tree, filled by `apply_bindings` and kept while the structure and the context stay.
        BindTable bind_table;
        mutable UiWorkCounters counters;
        mutable SharedTextCache text_cache;
        // The debug layout check runs after every layout sync in a build that has it. A test that edits elements behind
        // the engine's back on purpose switches it off. `abort_on_layout_mismatch`: a difference ends the process
        // (after naming the element), so a suite cannot pass over it; a test of the check itself turns that off.
        bool verify_layout = true;
        bool abort_on_layout_mismatch = true;
        // Relayout boundaries (a fixed-size element) that a change below them marked, and that the next layout pass
        // lays out from where they are.
        std::vector<ElementId> layout_roots;
        // Boundaries whose picture can never be replayed (they record again every frame), and the `IPaint`s whose
        // version decides when a picture is recorded again. Paint marks them each frame; entries whose element is gone
        // leave.
        std::vector<ElementId> picture_volatile;
        struct PaintWatch {
            ElementId element{};
            const IPaint *paint = nullptr;
            std::uint64_t seen = 0;
        };
        std::vector<PaintWatch> picture_watches;
        // Goes up on every `mark_dirty`. Work that reads a pointer or a hit test (the hover target) repeats only when
        // it moved, the pointer moved, or a layout pass ran.
        std::uint64_t epoch = 0;
        // Something the ItemsControl windows depend on may have changed (a layout pass ran, a scroll moved, a list was
        // edited): the bind pass checks every ItemsControl against it once and clears it.
        bool windows_dirty = true;

        // The element `id` names, or why there is none. A stale id (its row left the window, its slot was reused) and
        // an id the tree no longer matches (children edited by hand without `reindex`) are errors, never a pointer to
        // another element or to freed memory. The pointer is valid until the next structural edit of the tree.
        [[nodiscard]] std::expected<Element *, ElementLookupError> find(ElementId id);
        [[nodiscard]] std::expected<const Element *, ElementLookupError> find(ElementId id) const;

        // Walks the whole tree: gives an id to every element that has none (an ItemTemplate's subtree included), sets
        // every `parent`, brings every slot's place in the tree up to date, and frees the slots whose element is gone.
        // Elements that already have an id keep it. Parse and `make_document` call it; call it again after editing
        // `children` or `generated_items` by hand.
        void reindex();
        std::optional<AssetId> stylesheet;
        // The `{binding path}` text behind every BindingId the markup names, item templates included, so a bind
        // error can name the path. parse_xml fills it; a `ui::Node` document has none, and the error shows the id.
        std::unordered_map<BindingId, std::string> binding_paths;

        // wind-129 layout dirty-gate: "external" triggers that invalidate layout for the WHOLE
        // document at once (unlike the per-Element fields above), since layout_stack's packing is
        // holistic — canvas geometry, window size (media queries), and stylesheet identity/generation
        // all affect every element's layout_rect simultaneously, not just one. Lives on UiDocument
        // (not UiInstance) because paint_document (paint.cpp) — called from CmdDrawUI in
        // opengl_backend.cpp with only a UiDocument*, no UiInstance* — needs to read/write these too;
        // prepare_top_canvas (canvas.cpp) reaches them the same way, via instance->document.
        // layout_computed_once starts false so the very first frame for a freshly spawned canvas
        // always computes layout instead of trying to "skip" a layout that never happened.
        // The hover and press the pointer put on the tree: which element, and what the target search was for, so a
        // frame where none of it moved does not search again.
        struct Interaction {
            ElementId hovered{};
            bool pressed = false;
            glm::vec2 pointer{-1.0e9f, -1.0e9f};
            bool pointer_down = false;
            std::uint64_t epoch = 0;
            std::uint64_t layout_passes = 0;
            bool valid = false;
            // The last motion pass left a clock running: elements move under a resting pointer.
            bool motion = false;
        };
        mutable Interaction interaction;
        mutable bool layout_computed_once = false;
        mutable render::Rect last_canvas_layout_rect{};
        mutable float last_media_width = 0.0f;
        mutable float last_media_height = 0.0f;
        mutable const Stylesheet *last_layout_sheet = nullptr;
        mutable std::uint64_t last_layout_sheet_generation = 0;
        // Which IUiPainter (opaque here — document.h is a public header and IUiPainter is declared in
        // the private src/ui/painter.h, so this is stored as `const void*`, the same way
        // Element::generated_owner is an opaque identity-only pointer) last actually measured this
        // document's hug-sized text. Text/Button/TextInput hug sizing (document.cpp's
        // measure_element_text) reads through whichever IUiPainter* layout() is given — a real painter's
        // shaped metrics differ from the nullptr-painter CPU fallback layout() uses when none is
        // registered for a window yet — so switching which painter (or none at all) resolves for this
        // canvas between two frames is exactly as layout-relevant as a stylesheet swap, even though no
        // Element field changed. Starts nullptr, matching layout_painter_for()'s own "no painter
        // registered" result, so a document laid out once with the fallback and then again once a real
        // painter registers doesn't spuriously look unchanged.
        mutable const void *last_layout_painter = nullptr;
        // Same idea for the painter's math font: a Math element measured before the font is registered gets a
        // fallback size, and nothing else would trigger a relayout once the real font arrives.
        mutable const void *last_layout_math_font = nullptr;
    };

    struct UiInstance {
        UiDocument document;
        std::optional<Stylesheet> stylesheet;
        std::optional<AssetId> loaded_document;
        std::optional<AssetId> loaded_stylesheet;
        std::vector<AssetId> loaded_extra_stylesheets;
        std::vector<AssetId> loaded_sheet_ids;
        ViewModel *loaded_data_context = nullptr;
        // Bind errors run_bind has already logged for this instance, so a missing binding logs once, not every
        // frame. An instance rebuilt for a new document starts empty and logs again.
        std::unordered_set<std::string> reported_bind_errors;
        // The stylesheet generation and bind-table build the layout-under-pseudo-class check last ran for (debug
        // builds).
        std::uint64_t pseudo_checked_generation = 0;
        std::uint64_t pseudo_checked_builds = 0;
    };

    // Resolves an `<ItemTemplate src="...">` reference to the referenced file's raw XML text.
    // All `src` values in a document tree are relative to that document's own file, regardless of
    // include nesting depth — the resolver owns path composition. Returns nullopt if unreadable.
    using UiIncludeResolver = std::function<std::optional<std::string>(std::string_view src)>;

    // `fatal` null: a failure is only the returned UiError, nothing is reported. `fatal` set: a failure calls
    // `fatal->report`, which does not return.
    [[nodiscard]] std::expected<UiDocument, UiError> parse_xml(std::string_view xml, IFatalError *fatal = nullptr,
                                                               const ViewModel *data_context = nullptr,
                                                               const UiIncludeResolver &resolve_include = {});

    // Receives each failure of one apply_bindings pass. Unlike IFatalError::report it returns, because the pass
    // keeps walking: one missing binding must not leave the rest of the document unbound.
    class IBindErrorSink {
    public:
        virtual ~IBindErrorSink() = default;
        virtual void bind_error(std::string_view message) = 0;
    };

    // `catalog` resolves `{tr}` keys. Null leaves a `{tr}` element's text as the key and, when `errors`
    // is set, reports MissingString. Documents with no `{tr}` ignore it.
    // A binding the data context does not register is skipped, not fatal to the rest: every other element and
    // binding still binds. `errors` (null: none) gets one message per failure (naming the path from
    // `binding_paths`), and the result is the first error. Nothing here is fatal; the caller decides.
    //
    // The first call, and any after the data context, the catalog, or the tree's structure changed, reads every binding
    // and records a slot for each (`UiDocument::bind_table`). Every other call compares each slot's property version
    // with the one it saw and reads only the slots that moved, marking what they feed dirty (`dirty_self`). A document
    // whose elements were edited behind the bind pass (a field a binding fills) keeps that edit until the property
    // changes. `stylesheet` lets a change of a pseudo-class state (a command that became executable, a checkbox) tell
    // which elements restyle; null marks them all.
    std::expected<void, UiError> apply_bindings(UiDocument &document, ViewModel &data_context,
                                                IBindErrorSink *errors = nullptr,
                                                const engine::loc::Catalog *catalog = nullptr,
                                                const Stylesheet *stylesheet = nullptr);

    void layout(UiDocument &document, const render::Rect &canvas_rect);

    [[nodiscard]] Element *find_by_kind(Element &root, ElementKind kind);
    [[nodiscard]] const Element *find_by_kind(const Element &root, ElementKind kind);

    // First element, depth first, whose `id` attribute is `id`. Does not look into ItemTemplate or
    // generated rows.
    [[nodiscard]] Element *find_by_id(Element &root, std::string_view id);

    // Scrolls `scroller` (vertically scrollable, and `items_control` itself or an ancestor of it) so item
    // `index` of the ItemsControl's items_source lies inside the scroller's box. Reads the rows of the last
    // layout and assumes they share one height, so it works on a virtualized list whose item has no Element
    // yet. Sets Element::scroll_y only: a `scroll-y` binding on the scroller reads its own value back on the
    // next bind. False when the ItemsControl has no laid-out row or `index` is past the items.
    bool scroll_item_into_view(Element &scroller, const Element &items_control, std::size_t index);

    // Finds the generated Element currently stamped with this exact Element::generated_owner value
    // (see that field's comment) — used by canvas.cpp's drag write-back to re-resolve which item
    // ViewModel a drag started inside an ItemsControl/ItemTemplate still belongs to, on every frame of
    // the drag, rather than holding a ViewModel* across frames without revalidating it. `owner ==
    // nullptr` always returns nullptr (no Element is ever a "generated" root with a null owner in a
    // way that should match).
    [[nodiscard]] Element *find_by_generated_owner(Element &root, const void *owner);

    // Sibling paint/hit-test order: stable sort by z_index ascending (low first = behind, matching
    // UiCanvas::order), tie-broken by document order. z_index == 0 everywhere (the default) leaves
    // order unchanged. Paint iterates this forward; hit-testing iterates it in reverse (topmost
    // first).
    [[nodiscard]] std::vector<Element *> child_stacking_order(std::vector<Element> &children);

    // Hit-test bounds: layout_rect for an untransformed element (the common case, byte-identical to
    // today), otherwise the axis-aligned bounding box of the element's rotated+scaled corners about
    // its own center. This is an AABB approximation, not a precise oriented-rect test - a rotated
    // element's hit area is slightly generous at its corners.
    [[nodiscard]] render::Rect hit_bounds(const Element &element);

    // Topmost interactive element under (x, y): prunes by hit_bounds() containment, visits siblings
    // in reverse stacking order (highest z-index / last-drawn first), returns the first element that
    // is a Button or Checkbox, or has a bound `command` or `drag` (any element kind), or a Viewport
    // with a camera binding, or a Label whose `user-select` makes its text selectable, or nullptr.
    // A Label inside a Button or Checkbox is not a selection hit — the control keeps the click.
    // Viewport camera inverses the pointer for descendants and clips to the unpanned layout_rect.
    // Shared by click resolution (canvas.cpp) and hover resolution (paint.cpp).
    [[nodiscard]] Element *hit_test(Element &root, float x, float y);

    // Appends the rectangle (layout space) of every interactive element under `root` that hit_test() can return,
    // selectable labels aside: a Button, Checkbox, TextInput, ScrollView, scrollable or Viewport-with-camera element,
    // an element with a bound `command` or `drag` (a Slider included), and an open Popup at its shown position. An
    // interactive element is one rectangle: its children are not visited. Hidden elements and item templates are
    // skipped. `UiWindow` turns these into drag exclusions.
    void collect_interactive_rects(const Element &root, std::vector<render::Rect> &out);

    // The cursor at (x, y): the topmost shown element under the point, any kind, interactive or not, walking the
    // same stacking order, popups, scroll and Viewport cameras as hit_test(). Its `cursor`, or the nearest
    // ancestor's that is not Auto. When the whole chain is Auto, Text over a TextInput or a selectable Label, else
    // Default. Default when nothing is under the point.
    [[nodiscard]] Cursor cursor_at(Element &root, float x, float y);

    // Margin / border / content in canvas layout space (after ancestor scroll and Viewport cameras,
    // before the canvas scale/offset). Border is hit_bounds (the layout rect, or its AABB when the
    // element is rotated or scaled). Margin expands that box; content insets it by padding. Border
    // width does not shrink content.
    struct LayoutBoxes {
        render::Rect margin{};
        render::Rect border{};
        render::Rect content{};
    };

    // Deepest visible element under (x, y), including Label / Stack / Image. Same descent as
    // hit_test (z-index, scroll, Viewport camera, display:none, visibility) without the interactive
    // filter. ItemTemplate is skipped. `element` is null when nothing contains the point.
    struct VisualHit {
        Element *element = nullptr;
        LayoutBoxes boxes{};
    };

    [[nodiscard]] VisualHit hit_test_visual(Element &root, float x, float y);

    // Boxes of `target` in the same space as VisualHit::boxes. `target` must be inside `root`
    // (including a display:none node). Boxes are empty when it is not.
    [[nodiscard]] LayoutBoxes layout_boxes(Element &root, const Element &target);

    [[nodiscard]] inline bool has_viewport_camera(const Element &element) noexcept {
        return is_bound(element.pan_x_binding) || is_bound(element.pan_y_binding) || is_bound(element.zoom_binding);
    }

    [[nodiscard]] inline float viewport_zoom(float zoom) noexcept { return zoom > 0.0f ? zoom : 1.0f; }

    // Inverse of the Viewport paint camera: displayed = O + Z * (layout - O + P).
    [[nodiscard]] inline glm::vec2 viewport_to_display(glm::vec2 origin, glm::vec2 pan, float zoom,
                                                       glm::vec2 layout) noexcept {
        const float z = viewport_zoom(zoom);
        return origin + z * (layout - origin + pan);
    }

    [[nodiscard]] inline glm::vec2 inverse_viewport_pointer(const Element &viewport, glm::vec2 pointer) noexcept {
        const glm::vec2 origin{viewport.layout_rect.x, viewport.layout_rect.y};
        const float z = viewport_zoom(viewport.zoom);
        return origin + (pointer - origin) / z - glm::vec2{viewport.pan_x, viewport.pan_y};
    }

    // Pan that keeps `pointer` (display/layout space) on the same content point after zoom changes.
    [[nodiscard]] inline glm::vec2 viewport_pan_after_zoom(glm::vec2 origin, glm::vec2 pan, float zoom, float new_zoom,
                                                           glm::vec2 pointer) noexcept {
        const float z = viewport_zoom(zoom);
        const float nz = viewport_zoom(new_zoom);
        return (pointer - origin) * (1.0f / nz - 1.0f / z) + pan;
    }

    // Innermost Viewport whose clip contains `pointer` after ancestor camera inverses. Used by wheel
    // zoom so a node under the cursor still zooms its enclosing Viewport.
    [[nodiscard]] Element *find_viewport_at(Element &root, float x, float y);

    [[nodiscard]] inline bool is_scrollable_y(const Element &element) noexcept {
        return element.overflow_y == Overflow::Scroll ||
               (element.overflow_y == Overflow::Auto && element.max_scroll_y > 0.0f);
    }

    [[nodiscard]] inline bool is_scrollable_x(const Element &element) noexcept {
        return element.overflow_x == Overflow::Scroll ||
               (element.overflow_x == Overflow::Auto && element.max_scroll_x > 0.0f);
    }

    [[nodiscard]] inline bool is_scrollable(const Element &element) noexcept {
        return is_scrollable_y(element) || is_scrollable_x(element);
    }

    [[nodiscard]] render::Rect scrollbar_track_rect(const Element &element, float ui_scale = 1.0f) noexcept;
    [[nodiscard]] render::Rect scrollbar_thumb_rect(const Element &element, float ui_scale = 1.0f) noexcept;

    // Innermost scrollable container whose clip contains `pointer`. Used by wheel scrolling.
    [[nodiscard]] Element *find_scrollable_at(Element &root, float x, float y);

} // namespace engine::ui
