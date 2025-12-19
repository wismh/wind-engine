#include <engine/ui/canvas.h>

#include <engine/loc/catalog.h>
#include <engine/ui/document.h>

#include "painter.h"
#include "ui/input_batch.h"
#include "ui/text_select.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::ui {
namespace {

const engine::loc::Catalog* catalog_for(ecs::World& world) {
    return &world.ctx<engine::loc::Catalog>();
}

struct CanvasHit {
    int order = 0;
    std::uint32_t index = 0;
    ecs::Entity entity{};
};

render::Rect scaled_fit_rect(glm::vec2 reference_size, float window_width, float window_height) {
    if (reference_size.x <= 0.0f || reference_size.y <= 0.0f) {
        return render::Rect{0.0f, 0.0f, window_width, window_height};
    }
    const float scale = std::min(window_width / reference_size.x, window_height / reference_size.y);
    const float scaled_w = reference_size.x * scale;
    const float scaled_h = reference_size.y * scale;
    return render::Rect{
            (window_width - scaled_w) * 0.5f,
            (window_height - scaled_h) * 0.5f,
            scaled_w,
            scaled_h,
    };
}

}

WindowSize window_size_for(ecs::World& world, WindowId id) {
    const WindowSizes& sizes = world.ctx<WindowSizes>();
    const auto it = sizes.sizes.find(id);
    return it == sizes.sizes.end() ? WindowSize{} : it->second;
}

UiPointer& pointer_for(ecs::World& world, WindowId id) {
    if (id == kPrimaryWindow) {
        return world.ctx<UiPointer>();
    }
    return world.ctx<UiPointers>().pointers[id];
}

UiCanvasSpace canvas_layout_space(const render::Rect& rect, UiFit fit, glm::vec2 reference_size) {
    if (fit == UiFit::ScaleWithScreenSize && reference_size.x > 0.0f && reference_size.y > 0.0f) {
        return UiCanvasSpace{
                render::Rect{0.0f, 0.0f, reference_size.x, reference_size.y},
                glm::vec2{rect.x, rect.y},
                rect.w / reference_size.x,
                true,
        };
    }
    return UiCanvasSpace{rect, glm::vec2{0.0f, 0.0f}, 1.0f, false};
}

void apply_canvas_fit(ecs::World& world) {
    auto view = world.view<UiCanvas>();
    for (ecs::Entity entity : view) {
        UiCanvas& canvas = view.get<UiCanvas>(entity);
        const WindowSize size = window_size_for(world, canvas.window);
        if (canvas.fit == UiFit::FillWindow) {
            canvas.rect = render::Rect{
                    0.0f,
                    0.0f,
                    static_cast<float>(size.width),
                    static_cast<float>(size.height),
            };
        } else if (canvas.fit == UiFit::ScaleWithScreenSize) {
            canvas.rect = scaled_fit_rect(
                    canvas.reference_size, static_cast<float>(size.width), static_cast<float>(size.height));
        }
    }
}

void begin_frame(ecs::World& world) {
    world.ctx<MouseConsumed>().consumed_windows.clear();
    apply_canvas_fit(world);
}

namespace {

bool is_utf8_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t prev_utf8_char(std::string_view s, std::size_t pos) {
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && is_utf8_continuation(s[pos])) {
        --pos;
    }
    return pos;
}

std::size_t next_utf8_char(std::string_view s, std::size_t pos) {
    if (pos >= s.size()) {
        return s.size();
    }
    ++pos;
    while (pos < s.size() && is_utf8_continuation(s[pos])) {
        ++pos;
    }
    return pos;
}

// Which UTF-8 char boundary in element.text a click at real-screen-pixel `click_x` is closest to
// — snaps to whichever side of a glyph the click is nearer, same convention every text editor
// uses. Reads back Element::painted_font_size_px/painted_content_origin_x (paint.cpp) rather than
// re-resolving CSS length units (padding, em font-size) here, so this can never disagree with
// where paint.cpp actually drew the text/caret. Falls back to the end of the text when the
// element has never painted yet (painted_font_size_px == 0) or no painter is registered for this
// window (headless engine_tests) — same degraded-but-safe fallback the layout hug-sizing CPU path
// already accepts elsewhere.
std::size_t caret_index_for_click(ecs::World& world, const Element& element, float click_x, WindowId window) {
    if (element.painted_font_size_px <= 0.0f) {
        return element.text.size();
    }
    IUiPainter* painter = layout_painter_for(world, window);
    if (painter == nullptr) {
        return element.text.size();
    }
    const float target = click_x - element.painted_content_origin_x;
    std::size_t pos = 0;
    float measured_w = 0.0f;
    while (pos < element.text.size()) {
        const std::size_t next = next_utf8_char(element.text, pos);
        const float next_w =
                painter->measure_text(std::string_view(element.text).substr(0, next), element.font_family,
                        element.painted_font_size_px)
                        .x;
        if (target < (measured_w + next_w) * 0.5f) {
            return pos;
        }
        pos = next;
        measured_w = next_w;
    }
    return pos;
}

// Shared result of resolve_pointer_hit(): everything a caller needs to both resolve this hit
// (command/drag lookup) and, for handle_pointer()'s drag case, capture enough geometry to keep
// tracking the drag on later Move events without re-hit-testing.
struct PointerHit {
    Element* element = nullptr;
    UiCanvas* canvas = nullptr;
    ecs::Entity entity{};
    UiCanvasSpace space{};
};

struct PreparedCanvas {
    UiCanvas* canvas = nullptr;
    UiInstance* instance = nullptr;
    ecs::Entity entity{};
    UiCanvasSpace space{};
    glm::vec2 layout_pointer{};
};

// `batch` is nullptr for every call outside run_input() (every direct test call, and the two
// no-batch resolve_pointer_hit()/handle_wheel() default paths below) — always a full recompute,
// unchanged from before Крок 4. Only run_input()'s *_for_run_input() entry points (input_batch.h)
// pass a real batch, so a canvas entity prepare_top_canvas() already fully bound+styled+laid out
// earlier in the same run_input() call is reused instead of recomputed — see input_batch.h for
// why this cache lives on run_input()'s stack rather than in ctx<>().
std::optional<PreparedCanvas> prepare_top_canvas(
        ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch = nullptr) {
    std::vector<CanvasHit> hits;
    {
        auto view = world.view<UiCanvas>();
        for (ecs::Entity entity : view) {
            const UiCanvas& canvas = view.get<UiCanvas>(entity);
            if (canvas.window != window) {
                continue;
            }
            if (!rect_contains(canvas.rect, x, y)) {
                continue;
            }
            hits.push_back(CanvasHit{canvas.order, entity.index, entity});
        }
    }
    if (hits.empty()) {
        return std::nullopt;
    }

    std::stable_sort(hits.begin(), hits.end(), [](const CanvasHit& a, const CanvasHit& b) {
        if (a.order != b.order) {
            return a.order > b.order;
        }
        return a.index > b.index;
    });

    const ecs::Entity entity = hits.front().entity;
    UiCanvas& canvas = world.get<UiCanvas>(entity);
    UiInstance* instance = world.try_get<UiInstance>(entity);
    if (instance == nullptr) {
        return std::nullopt;
    }

    // Layout does not depend on scroll_x/scroll_y (paint-time transform only, see paint.cpp), so
    // reusing a batch-cached layout_rect while wheel-scrolling within the same run_input() call
    // is safe. Cheap regardless of cache hit/miss — no bind/layout work — so always recomputed:
    // canvas.rect (hence space) never changes between events inside one run_input() call anyway,
    // but computing it fresh keeps this branch trivial to reason about.
    const WindowSize size = window_size_for(world, canvas.window);
    const UiCanvasSpace space = canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);

    const bool reuse_cached = batch != nullptr && batch->contains(entity);
    if (!reuse_cached) {
        const Stylesheet* sheet = nullptr;
        if (instance->stylesheet) {
            sheet = &*instance->stylesheet;
        }
        if (canvas.data_context) {
            (void) apply_bindings(instance->document, *canvas.data_context, nullptr, catalog_for(world));
        }
        const float media_width = space.reference_space ? space.layout_rect.w : static_cast<float>(size.width);
        const float media_height = space.reference_space ? space.layout_rect.h : static_cast<float>(size.height);
        IUiPainter* layout_painter = layout_painter_for(world, window);

        UiDocument& document = instance->document;
        // wind-129 layout dirty-gate. layout_state_changed() MUST run unconditionally (never as a
        // trailing operand of a short-circuiting `||`, where it would simply not be called once an
        // earlier operand is already true) — it has a side effect (refreshes the per-Element cache
        // copies it compares against), and skipping that refresh this frame would leave next
        // frame's comparison against a stale copy: a spurious "changed" at best, or — if the stale
        // copy happens to equal the new value — a false "unchanged" that freezes layout for real.
        const bool per_element_changed = layout_state_changed(document.root);
        const std::uint64_t sheet_generation = sheet != nullptr ? sheet->generation : 0;
        const bool layout_dirty = per_element_changed || !document.layout_computed_once ||
                document.last_canvas_layout_rect != space.layout_rect || document.last_media_width != media_width ||
                document.last_media_height != media_height || document.last_layout_sheet != sheet ||
                document.last_layout_sheet_generation != sheet_generation ||
                document.last_layout_painter != static_cast<const void*>(layout_painter) ||
                document.last_layout_math_font != math_font_identity(layout_painter);
        if (layout_dirty) {
            apply_layout_style(document.root, sheet, media_width, media_height);
            layout(document, space.layout_rect, layout_painter);
            document.layout_computed_once = true;
            document.last_canvas_layout_rect = space.layout_rect;
            document.last_media_width = media_width;
            document.last_media_height = media_height;
            document.last_layout_sheet = sheet;
            document.last_layout_sheet_generation = sheet_generation;
            document.last_layout_painter = layout_painter;
            document.last_layout_math_font = math_font_identity(layout_painter);
        }
        if (batch != nullptr) {
            batch->mark(entity);
        }
    }

    const glm::vec2 layout_pointer{(x - space.offset.x) / space.scale, (y - space.offset.y) / space.scale};
    return PreparedCanvas{&canvas, instance, entity, space, layout_pointer};
}

// Shared by handle_pointer() and update_pointer_hover(): finds the topmost element under (x, y),
// rebuilding bindings/layout the same way for both so a hover hit test sees the exact same
// element a click at that position would. Layout uses the window's IUiPainter when registered
// (UiLayoutPainters) so hug text metrics match paint_document; otherwise the CPU fallback.
// Sets MouseConsumed as a side effect whenever it finds a hit (matching the previous
// handle_pointer() behavior) — both callers want that.
std::optional<PointerHit> resolve_pointer_hit(
        ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch = nullptr) {
    const std::optional<PreparedCanvas> prepared = prepare_top_canvas(world, x, y, window, batch);
    if (!prepared) {
        return std::nullopt;
    }

    Element* hit = hit_test(prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (hit == nullptr) {
        return std::nullopt;
    }

    world.ctx<MouseConsumed>().consumed_windows.insert(window);
    return PointerHit{hit, prepared->canvas, prepared->entity, prepared->space};
}

// Shared axis math for handle_pointer()'s drag-start and update_drag()'s continuation: maps a
// window-space (x, y) into the drag's layout-space rect (the same `(v - offset) / scale` transform
// resolve_pointer_hit() applies before hit-testing) and returns the clamped [0,1] fraction along
// `orientation`'s axis of `rect`. No min/max/step — remapping a raw fraction into a domain-specific
    // range belongs to the game's ViewModel, not the engine.
float compute_drag_fraction(StackDirection orientation, const render::Rect& rect, glm::vec2 space_offset,
        float space_scale, float x, float y) {
    const float local_x = (x - space_offset.x) / space_scale;
    const float local_y = (y - space_offset.y) / space_scale;
    const float t = orientation == StackDirection::Horizontal
            ? (rect.w > 0.0f ? (local_x - rect.x) / rect.w : 0.0f)
            : (rect.h > 0.0f ? (local_y - rect.y) / rect.h : 0.0f);
    return std::clamp(t, 0.0f, 1.0f);
}

bool build_element_path(const Element& current, const Element* target, std::vector<std::size_t>& path) {
    if (&current == target) {
        return true;
    }
    for (std::size_t i = 0; i < current.children.size(); ++i) {
        path.push_back(i);
        if (build_element_path(current.children[i], target, path)) {
            return true;
        }
        path.pop_back();
    }
    for (std::size_t i = 0; i < current.generated_items.size(); ++i) {
        path.push_back(i | 0x80000000ULL);
        if (build_element_path(current.generated_items[i], target, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

std::vector<std::size_t> find_element_path(const Element& root, const Element* target) {
    std::vector<std::size_t> path;
    build_element_path(root, target, path);
    return path;
}

Element* resolve_element_path(Element& root, const std::vector<std::size_t>& path) {
    Element* curr = &root;
    for (std::size_t step : path) {
        if ((step & 0x80000000ULL) != 0) {
            const std::size_t idx = step & ~0x80000000ULL;
            if (idx < curr->generated_items.size()) {
                curr = &curr->generated_items[idx];
            } else {
                return nullptr;
            }
        } else {
            if (step < curr->children.size()) {
                curr = &curr->children[step];
            } else {
                return nullptr;
            }
        }
    }
    return curr;
}

}

namespace {

enum class TextSelectUnit : std::uint8_t { Character, Word, All };

// Lives only for the gesture that started on pointer-down. A word drag unions this seed range with
// the word under the pointer; All does not shrink. Cleared by the next pointer-down, not stored on Element.
struct TextSelectGesture {
    TextSelectUnit unit = TextSelectUnit::Character;
    std::size_t origin_begin = 0;
    std::size_t origin_end = 0;
};

struct UiTextSelectGestures {
    std::unordered_map<WindowId, TextSelectGesture> by_window;
};

// One box on the row under the pointer. Several segments share a y when the label has an inline formula.
const PaintedTextLine* painted_segment_at(const std::vector<PaintedTextLine>& lines, float x, float y) {
    if (lines.empty()) {
        return nullptr;
    }
    const PaintedTextLine* row = nullptr;
    float nearest_dy = 0.0f;
    bool row_contains = false;
    for (const PaintedTextLine& line : lines) {
        const float height = std::max(line.height, 0.0f);
        const bool inside = y >= line.y && y < line.y + height;
        const float dy = std::abs(y - (line.y + height * 0.5f));
        if (inside && !row_contains) {
            row = &line;
            row_contains = true;
            nearest_dy = dy;
        } else if (!row_contains && (row == nullptr || dy < nearest_dy)) {
            row = &line;
            nearest_dy = dy;
        }
    }
    if (row == nullptr) {
        return nullptr;
    }
    constexpr float kRowEpsilon = 0.01f;
    const PaintedTextLine* best = nullptr;
    float best_dist = 0.0f;
    bool best_interior = false;
    for (const PaintedTextLine& line : lines) {
        if (std::abs(line.y - row->y) > kRowEpsilon) {
            continue;
        }
        const float right = line.x + line.width;
        const bool interior = line.width > 0.0f && x > line.x && x < right;
        float dist = 0.0f;
        if (x < line.x) {
            dist = line.x - x;
        } else if (x > right) {
            dist = x - right;
        }
        const bool closer = best == nullptr || (interior && !best_interior) ||
                            (interior == best_interior && dist < best_dist) ||
                            (interior == best_interior && dist == best_dist && line.atomic && best != nullptr &&
                                    !best->atomic);
        if (closer) {
            best = &line;
            best_dist = dist;
            best_interior = interior;
        }
    }
    return best;
}

struct LabelPointerHit {
    std::size_t index = 0;
    // Set when the pointer is over a formula box, so a word gesture takes that source span even
    // after the caret has snapped to one of its edges.
    std::optional<TextRange> formula;
};

LabelPointerHit label_pointer_hit(
        ecs::World& world, const Element& element, float click_x, float click_y, WindowId window) {
    LabelPointerHit hit;
    hit.index = element.text.size();
    if (element.painted_text_lines.empty() || element.painted_font_size_px <= 0.0f) {
        return hit;
    }
    IUiPainter* painter = layout_painter_for(world, window);
    if (painter == nullptr) {
        return hit;
    }
    const PaintedTextLine* line = painted_segment_at(element.painted_text_lines, click_x, click_y);
    if (line == nullptr) {
        return hit;
    }
    if (line->atomic) {
        hit.formula = TextRange{line->begin, line->end};
        hit.index = !(line->width > 0.0f) || click_x >= line->x + line->width * 0.5f ? line->end : line->begin;
        return hit;
    }
    const bool mapped = line->source_of_drawn.size() == line->drawn.size() + 1 && !line->drawn.empty();
    const std::string_view slice = mapped ? std::string_view(line->drawn)
                                          : std::string_view(element.text).substr(line->begin, line->end - line->begin);
    if (click_x <= line->x) {
        hit.index = mapped ? line->source_of_drawn.front() : line->begin;
        return hit;
    }
    std::size_t pos = 0;
    float measured_w = 0.0f;
    while (pos < slice.size()) {
        const std::size_t next = std::min(next_utf8_char(slice, pos), slice.size());
        if (next <= pos) {
            break;
        }
        const float next_w =
                painter->measure_text(slice.substr(0, next), element.font_family, element.painted_font_size_px).x;
        if ((click_x - line->x) < (measured_w + next_w) * 0.5f) {
            hit.index = mapped ? line->source_of_drawn[pos] : line->begin + pos;
            return hit;
        }
        pos = next;
        measured_w = next_w;
    }
    hit.index = mapped ? line->source_of_drawn.back() : line->end;
    return hit;
}

std::size_t text_index_for_click(ecs::World& world, const Element& element, float x, float y, WindowId window) {
    if (element.kind == ElementKind::Label) {
        return label_pointer_hit(world, element, x, y, window).index;
    }
    return caret_index_for_click(world, element, x, window);
}

void place_text_selection(ecs::World& world, WindowId window, ecs::Entity entity, Element& element, float x, float y,
        std::uint8_t clicks) {
    const bool shift = world.ctx<UiModifierState>().modifiers[window].shift && element.focused;
    std::optional<std::size_t> kept;
    if (shift) {
        kept = element.selection_anchor.value_or(element.caret_position);
    }
    // set_focus clears selection_anchor, including on the element we are about to select.
    set_focus(world, window, entity, &element);

    const bool select_all =
            clicks >= 3 || (element.kind == ElementKind::Label && element.user_select == UserSelect::All);
    auto& gesture = world.ctx<UiTextSelectGestures>().by_window[window];
    if (select_all) {
        element.selection_anchor = 0;
        element.caret_position = element.text.size();
        gesture = TextSelectGesture{TextSelectUnit::All, 0, element.text.size()};
    } else if (clicks >= 2) {
        std::size_t index = 0;
        TextRange word;
        if (element.kind == ElementKind::Label) {
            const LabelPointerHit hit = label_pointer_hit(world, element, x, y, window);
            index = hit.index;
            word = hit.formula ? *hit.formula : label_word_range(element.text, index);
        } else {
            index = caret_index_for_click(world, element, x, window);
            word = word_range(element.text, index);
        }
        if (kept) {
            const std::size_t edge = index >= *kept ? word.end : word.begin;
            element.selection_anchor = *kept;
            element.caret_position = edge;
            gesture = TextSelectGesture{TextSelectUnit::Word, std::min(*kept, edge), std::max(*kept, edge)};
        } else {
            element.selection_anchor = word.begin;
            element.caret_position = word.end;
            gesture = TextSelectGesture{TextSelectUnit::Word, word.begin, word.end};
        }
    } else {
        const std::size_t index = text_index_for_click(world, element, x, y, window);
        if (kept) {
            element.selection_anchor = *kept;
            element.caret_position = index;
        } else {
            element.selection_anchor = index;
            element.caret_position = index;
        }
        gesture = TextSelectGesture{TextSelectUnit::Character, index, index};
    }
    element.caret_blink_timer = 0.0f;
}

void extend_text_selection(ecs::World& world, Element& element, float x, float y, WindowId window) {
    const auto& gestures = world.ctx<UiTextSelectGestures>().by_window;
    const auto it = gestures.find(window);
    const TextSelectUnit unit = it == gestures.end() ? TextSelectUnit::Character : it->second.unit;
    if (unit == TextSelectUnit::All) {
        return;
    }
    if (element.kind == ElementKind::Label) {
        const LabelPointerHit hit = label_pointer_hit(world, element, x, y, window);
        if (unit == TextSelectUnit::Word && it != gestures.end()) {
            const TextRange word = hit.formula ? *hit.formula : label_word_range(element.text, hit.index);
            element.selection_anchor = std::min(it->second.origin_begin, word.begin);
            element.caret_position = std::max(it->second.origin_end, word.end);
        } else {
            element.caret_position = hit.index;
        }
    } else {
        const std::size_t index = caret_index_for_click(world, element, x, window);
        if (unit == TextSelectUnit::Word && it != gestures.end()) {
            const TextRange word = word_range(element.text, index);
            element.selection_anchor = std::min(it->second.origin_begin, word.begin);
            element.caret_position = std::max(it->second.origin_end, word.end);
        } else {
            element.caret_position = index;
        }
    }
    element.caret_blink_timer = 0.0f;
}

void handle_pointer_impl(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch,
        bool primary_button, std::uint8_t clicks) {
    const std::optional<PointerHit> hit = resolve_pointer_hit(world, x, y, window, batch);
    if (!hit) {
        // Right-click does not move focus, so a selection stays put.
        if (primary_button) {
            clear_focus(world, window);
        }
        return;
    }

    const bool text_input = hit->element->kind == ElementKind::TextInput;
    const bool selectable_label = label_text_selectable(*hit->element);
    if ((text_input || selectable_label) && primary_button) {
        place_text_selection(world, window, hit->entity, *hit->element, x, y, clicks);
    } else if (primary_button && !text_input && !selectable_label) {
        clear_focus(world, window);
    }

    if (hit->element->kind == ElementKind::Viewport && has_viewport_camera(*hit->element) && hit->canvas->data_context) {
        world.ctx<UiActivePans>().pans[window] = ActivePan{
                hit->entity,
                hit->element->pan_x_binding,
                hit->element->pan_y_binding,
                hit->element->zoom_binding,
                glm::vec2{x, y},
                hit->space.offset,
                hit->space.scale,
                hit->element->generated_owner,
        };
    } else if (is_bound(hit->element->drag_binding) && hit->canvas->data_context) {
        // A drag-bound element generated inside an ItemsControl/ItemTemplate has its `drag`
        // binding registered on the *item* ViewModel (Element::generated_owner, freshly resolved
        // by the apply_bindings() resolve_pointer_hit() just ran), not the canvas's own
        // data_context — writing to data_context there would silently no-op forever.
        ViewModel* target = hit->element->generated_owner != nullptr
                ? static_cast<ViewModel*>(const_cast<void*>(hit->element->generated_owner))
                : hit->canvas->data_context.get();
        const float fraction = compute_drag_fraction(hit->element->drag_orientation, hit->element->layout_rect,
                hit->space.offset, hit->space.scale, x, y);
        target->write_property_float(hit->element->drag_binding, fraction);
        world.ctx<UiActiveDrags>().drags[window] = ActiveDrag{
                hit->entity,
                hit->element->drag_binding,
                hit->element->layout_rect,
                hit->space.offset,
                hit->space.scale,
                hit->element->drag_orientation,
                hit->element->generated_owner,
        };
    }

    bool clicked_scrollbar = false;
    const glm::vec2 local_pointer{
            (x - hit->space.offset.x) / hit->space.scale, (y - hit->space.offset.y) / hit->space.scale};
    if (is_scrollable_y(*hit->element)) {
        const render::Rect track = scrollbar_track_rect(*hit->element);
        const render::Rect thumb = scrollbar_thumb_rect(*hit->element);
        if (track.w > 0.0f && track.h > 0.0f && rect_contains(track, local_pointer.x, local_pointer.y)) {
            clicked_scrollbar = true;
            UiInstance& inst = world.get<UiInstance>(hit->entity);
            if (rect_contains(thumb, local_pointer.x, local_pointer.y)) {
                hit->element->scrollbar_dragging = true;
                world.ctx<UiActiveScrollbars>().drags[window] = ActiveScrollbarDrag{
                        hit->entity,
                        find_element_path(inst.document.root, hit->element),
                        hit->element->generated_owner,
                        local_pointer.y,
                        hit->element->scroll_y,
                        track.h,
                        thumb.h,
                        hit->element->max_scroll_y,
                        hit->space.offset,
                        hit->space.scale,
                        hit->element->scroll_y_binding,
                };
            } else {
                const float available = track.h - thumb.h;
                if (available > 0.0f) {
                    const float target_thumb_y = local_pointer.y - track.y - thumb.h * 0.5f;
                    const float fraction = std::clamp(target_thumb_y / available, 0.0f, 1.0f);
                    hit->element->scroll_y = fraction * hit->element->max_scroll_y;
                    if (is_bound(hit->element->scroll_y_binding) && hit->canvas->data_context) {
                        ViewModel* target = hit->element->generated_owner != nullptr
                                ? static_cast<ViewModel*>(const_cast<void*>(hit->element->generated_owner))
                                : hit->canvas->data_context.get();
                        target->write_property_float(hit->element->scroll_y_binding, hit->element->scroll_y);
                    }
                }
            }
        }
    }

    if (!clicked_scrollbar && hit->element->kind == ElementKind::Checkbox) {
        hit->element->checked = !hit->element->checked;
        if (is_bound(hit->element->checked_binding) && hit->canvas->data_context) {
            ViewModel* target = hit->element->generated_owner != nullptr
                    ? static_cast<ViewModel*>(const_cast<void*>(hit->element->generated_owner))
                    : hit->canvas->data_context.get();
            target->write_property_float(hit->element->checked_binding, hit->element->checked ? 1.0f : 0.0f);
        }
    }

    if (!clicked_scrollbar && hit->element->kind != ElementKind::TextInput) {
        ICommand* command = hit->element->command;
        if (command == nullptr && is_bound(hit->element->command_binding) && hit->canvas->data_context) {
            command = hit->canvas->data_context->find_command(hit->element->command_binding);
        }
        if (command != nullptr && command->can_execute()) {
            command->execute();
        }
    }
}

}

void handle_pointer(ecs::World& world, float x, float y, WindowId window, bool primary_button, std::uint8_t clicks) {
    handle_pointer_impl(world, x, y, window, nullptr, primary_button, clicks);
}

void handle_pointer_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch,
        bool primary_button, std::uint8_t clicks) {
    handle_pointer_impl(world, x, y, window, &batch, primary_button, clicks);
}

void update_text_selection(ecs::World& world, float x, float y, WindowId window) {
    if (!pointer_for(world, window).down) {
        return;
    }
    Element* element = focused_element(world, window);
    if (element == nullptr || element->disabled) {
        return;
    }
    if (element->kind != ElementKind::TextInput && !label_text_selectable(*element)) {
        return;
    }
    extend_text_selection(world, *element, x, y, window);
}

namespace {

void update_drag_impl(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch) {
    auto& scroll_drags = world.ctx<UiActiveScrollbars>().drags;
    if (const auto sit = scroll_drags.find(window); sit != scroll_drags.end()) {
        const ActiveScrollbarDrag& sdrag = sit->second;
        UiInstance* instance = world.try_get<UiInstance>(sdrag.canvas_entity);
        if (instance != nullptr) {
            Element* elem = resolve_element_path(instance->document.root, sdrag.path);
            if (elem != nullptr) {
                const float local_y = (y - sdrag.space_offset.y) / sdrag.space_scale;
                const float dy = local_y - sdrag.drag_start_pointer_y;
                const float available = sdrag.track_h - sdrag.thumb_h;
                if (available > 0.0f) {
                    const float delta_scroll = (dy / available) * sdrag.max_scroll_y;
                    elem->scroll_y = std::clamp(sdrag.drag_start_scroll_y + delta_scroll, 0.0f, sdrag.max_scroll_y);
                    if (is_bound(sdrag.scroll_y_binding)) {
                        UiCanvas* canvas = world.try_get<UiCanvas>(sdrag.canvas_entity);
                        if (canvas != nullptr && canvas->data_context) {
                            ViewModel* target = sdrag.owner != nullptr
                                    ? static_cast<ViewModel*>(const_cast<void*>(sdrag.owner))
                                    : canvas->data_context.get();
                            target->write_property_float(sdrag.scroll_y_binding, elem->scroll_y);
                        }
                    }
                }
            }
        }
    }

    auto& drags = world.ctx<UiActiveDrags>().drags;
    const auto it = drags.find(window);
    if (it == drags.end()) {
        return;
    }
    const ActiveDrag& drag = it->second;
    UiCanvas* canvas = world.try_get<UiCanvas>(drag.canvas_entity);
    if (canvas == nullptr || !canvas->data_context) {
        drags.erase(it);
        return;
    }

    ViewModel* target = canvas->data_context.get();
    if (drag.owner != nullptr) {
        UiInstance* instance = world.try_get<UiInstance>(drag.canvas_entity);
        if (instance == nullptr) {
            drags.erase(it);
            return;
        }
        // Skip only if prepare_top_canvas() already fully bound+styled+laid out this exact
        // canvas earlier in the same run_input() batch (see input_batch.h UiInputBatchCache::
        // mark()) — that's the only thing that guarantees instance->document is fresh; a
        // batch==nullptr caller (every direct test call) always re-binds, unchanged from before
        // Крок 4.
        if (batch == nullptr || !batch->contains(drag.canvas_entity)) {
            (void) apply_bindings(instance->document, *canvas->data_context, nullptr, catalog_for(world));
        } else {
            ++batch->drag_or_pan_bindings_reused_count;
        }
        const Element* owner_element = find_by_generated_owner(instance->document.root, drag.owner);
        if (owner_element == nullptr) {
            drags.erase(it);
            return;
        }
        target = static_cast<ViewModel*>(const_cast<void*>(drag.owner));
    }

    const float fraction =
            compute_drag_fraction(drag.orientation, drag.rect, drag.space_offset, drag.space_scale, x, y);
    target->write_property_float(drag.value_binding, fraction);
}

}

void update_drag(ecs::World& world, float x, float y, WindowId window) {
    update_drag_impl(world, x, y, window, nullptr);
}

void update_drag_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch) {
    update_drag_impl(world, x, y, window, &batch);
}

void end_drag(ecs::World& world, WindowId window) {
    auto& scroll_drags = world.ctx<UiActiveScrollbars>().drags;
    if (const auto sit = scroll_drags.find(window); sit != scroll_drags.end()) {
        UiInstance* instance = world.try_get<UiInstance>(sit->second.canvas_entity);
        if (instance != nullptr) {
            Element* elem = resolve_element_path(instance->document.root, sit->second.path);
            if (elem != nullptr) {
                elem->scrollbar_dragging = false;
            }
        }
        scroll_drags.erase(sit);
    }
    world.ctx<UiActiveDrags>().drags.erase(window);
}

namespace {

// Same "only skip if prepare_top_canvas() already covered this canvas this batch" rule as
// update_drag_impl() above.
ViewModel* pan_target(ecs::World& world, const ActivePan& pan, UiCanvas& canvas, UiInputBatchCache* batch) {
    if (pan.owner == nullptr) {
        return canvas.data_context.get();
    }
    UiInstance* instance = world.try_get<UiInstance>(pan.canvas_entity);
    if (instance == nullptr) {
        return nullptr;
    }
    if (batch == nullptr || !batch->contains(pan.canvas_entity)) {
        (void) apply_bindings(instance->document, *canvas.data_context, nullptr, catalog_for(world));
    } else {
        ++batch->drag_or_pan_bindings_reused_count;
    }
    if (find_by_generated_owner(instance->document.root, pan.owner) == nullptr) {
        return nullptr;
    }
    return static_cast<ViewModel*>(const_cast<void*>(pan.owner));
}

void update_pan_impl(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch) {
    auto& pans = world.ctx<UiActivePans>().pans;
    const auto it = pans.find(window);
    if (it == pans.end()) {
        return;
    }
    ActivePan& pan = it->second;
    UiCanvas* canvas = world.try_get<UiCanvas>(pan.canvas_entity);
    if (canvas == nullptr || !canvas->data_context) {
        pans.erase(it);
        return;
    }
    ViewModel* target = pan_target(world, pan, *canvas, batch);
    if (target == nullptr) {
        pans.erase(it);
        return;
    }

    float zoom = 1.0f;
    if (is_bound(pan.zoom_binding)) {
        zoom = viewport_zoom(target->read_property_float(pan.zoom_binding).value_or(1.0f));
    }
    const float scale = pan.space_scale * zoom;
    const glm::vec2 delta{(x - pan.last_pointer.x) / scale, (y - pan.last_pointer.y) / scale};
    pan.last_pointer = glm::vec2{x, y};
    if (is_bound(pan.pan_x_binding)) {
        const float current = target->read_property_float(pan.pan_x_binding).value_or(0.0f);
        target->write_property_float(pan.pan_x_binding, current + delta.x);
    }
    if (is_bound(pan.pan_y_binding)) {
        const float current = target->read_property_float(pan.pan_y_binding).value_or(0.0f);
        target->write_property_float(pan.pan_y_binding, current + delta.y);
    }
}

}

void update_pan(ecs::World& world, float x, float y, WindowId window) {
    update_pan_impl(world, x, y, window, nullptr);
}

void update_pan_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch) {
    update_pan_impl(world, x, y, window, &batch);
}

void end_pan(ecs::World& world, WindowId window) {
    world.ctx<UiActivePans>().pans.erase(window);
}

namespace {

void handle_wheel_impl(ecs::World& world, float x, float y, float wheel_y, WindowId window, UiInputBatchCache* batch) {
    if (wheel_y == 0.0f) {
        return;
    }
    const std::optional<PreparedCanvas> prepared = prepare_top_canvas(world, x, y, window, batch);
    if (!prepared) {
        return;
    }

    Element* scrollable = find_scrollable_at(
            prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (scrollable != nullptr) {
        constexpr float kScrollStep = 40.0f;
        bool scrolled = false;
        if (is_scrollable_y(*scrollable)) {
            scrollable->scroll_y =
                    std::clamp(scrollable->scroll_y - wheel_y * kScrollStep, 0.0f, scrollable->max_scroll_y);
            if (is_bound(scrollable->scroll_y_binding) && prepared->canvas->data_context) {
                ViewModel* target = scrollable->generated_owner != nullptr
                        ? static_cast<ViewModel*>(const_cast<void*>(scrollable->generated_owner))
                        : prepared->canvas->data_context.get();
                target->write_property_float(scrollable->scroll_y_binding, scrollable->scroll_y);
            }
            scrolled = true;
        } else if (is_scrollable_x(*scrollable)) {
            scrollable->scroll_x =
                    std::clamp(scrollable->scroll_x - wheel_y * kScrollStep, 0.0f, scrollable->max_scroll_x);
            if (is_bound(scrollable->scroll_x_binding) && prepared->canvas->data_context) {
                ViewModel* target = scrollable->generated_owner != nullptr
                        ? static_cast<ViewModel*>(const_cast<void*>(scrollable->generated_owner))
                        : prepared->canvas->data_context.get();
                target->write_property_float(scrollable->scroll_x_binding, scrollable->scroll_x);
            }
            scrolled = true;
        }
        if (scrolled) {
            world.ctx<MouseConsumed>().consumed_windows.insert(window);
            return;
        }
    }

    if (!prepared->canvas->data_context) {
        return;
    }
    Element* viewport = find_viewport_at(
            prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (viewport == nullptr || !is_bound(viewport->zoom_binding)) {
        return;
    }

    ViewModel* target = viewport->generated_owner != nullptr
            ? static_cast<ViewModel*>(const_cast<void*>(viewport->generated_owner))
            : prepared->canvas->data_context.get();
    const float z = viewport_zoom(target->read_property_float(viewport->zoom_binding).value_or(viewport->zoom));
    const float new_z = std::clamp(z * std::pow(kViewportZoomStep, wheel_y), kViewportMinZoom, kViewportMaxZoom);
    const glm::vec2 origin{viewport->layout_rect.x, viewport->layout_rect.y};
    glm::vec2 pan{viewport->pan_x, viewport->pan_y};
    if (is_bound(viewport->pan_x_binding)) {
        pan.x = target->read_property_float(viewport->pan_x_binding).value_or(pan.x);
    }
    if (is_bound(viewport->pan_y_binding)) {
        pan.y = target->read_property_float(viewport->pan_y_binding).value_or(pan.y);
    }
    const glm::vec2 new_pan = viewport_pan_after_zoom(origin, pan, z, new_z, prepared->layout_pointer);
    target->write_property_float(viewport->zoom_binding, new_z);
    if (is_bound(viewport->pan_x_binding)) {
        target->write_property_float(viewport->pan_x_binding, new_pan.x);
    }
    if (is_bound(viewport->pan_y_binding)) {
        target->write_property_float(viewport->pan_y_binding, new_pan.y);
    }
    world.ctx<MouseConsumed>().consumed_windows.insert(window);
}

}

void handle_wheel(ecs::World& world, float x, float y, float wheel_y, WindowId window) {
    handle_wheel_impl(world, x, y, wheel_y, window, nullptr);
}

void handle_wheel_for_run_input(
        ecs::World& world, float x, float y, float wheel_y, WindowId window, UiInputBatchCache& batch) {
    handle_wheel_impl(world, x, y, wheel_y, window, &batch);
}

namespace {

void update_pointer_hover_impl(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache* batch) {
    const std::optional<PointerHit> hit = resolve_pointer_hit(world, x, y, window, batch);
    if (hit && is_scrollable_y(*hit->element)) {
        const glm::vec2 local_pointer{
                (x - hit->space.offset.x) / hit->space.scale, (y - hit->space.offset.y) / hit->space.scale};
        const render::Rect thumb = scrollbar_thumb_rect(*hit->element);
        hit->element->scrollbar_thumb_hovered = rect_contains(thumb, local_pointer.x, local_pointer.y);
    }
}

}

void update_pointer_hover(ecs::World& world, float x, float y, WindowId window) {
    update_pointer_hover_impl(world, x, y, window, nullptr);
}

void update_pointer_hover_for_run_input(
        ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch) {
    update_pointer_hover_impl(world, x, y, window, &batch);
}

ecs::Entity spawn_canvas(ecs::World& world, UiCanvas canvas, UiDocument document, std::optional<Stylesheet> stylesheet) {
    canvas.document.reset();
    const ecs::Entity entity = world.create();
    world.emplace<UiCanvas>(entity, std::move(canvas));
    world.emplace<UiInstance>(entity, UiInstance{std::move(document), std::move(stylesheet)});
    return entity;
}

namespace {

// Shared by handle_text_input/handle_key's Backspace/Delete/Ctrl+X/Ctrl+V paths — every one of
// them ends with "if bound, push element->text back to the ViewModel."
void write_text_binding(ecs::World& world, ecs::Entity canvas_entity, Element* element) {
    if (!is_bound(element->text_binding)) {
        return;
    }
    UiCanvas* canvas = world.try_get<UiCanvas>(canvas_entity);
    if (canvas == nullptr || !canvas->data_context) {
        return;
    }
    ViewModel* target = element->generated_owner != nullptr
            ? static_cast<ViewModel*>(const_cast<void*>(element->generated_owner))
            : canvas->data_context.get();
    if (target != nullptr) {
        target->write_property_string(element->text_binding, element->text);
    }
}

}

void clear_focus(ecs::World& world, WindowId window) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it != focus_map.end()) {
        if (it->second.element != nullptr) {
            it->second.element->focused = false;
            it->second.element->caret_blink_timer = 0.0f;
            it->second.element->selection_anchor.reset();
        }
        focus_map.erase(it);
    }
}

void set_focus(ecs::World& world, WindowId window, ecs::Entity canvas_entity, Element* element) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it != focus_map.end() && it->second.element != element) {
        if (it->second.element != nullptr) {
            it->second.element->focused = false;
            it->second.element->caret_blink_timer = 0.0f;
            it->second.element->selection_anchor.reset();
        }
    }
    if (element != nullptr) {
        element->focused = true;
        element->caret_blink_timer = 0.0f;
        element->selection_anchor.reset();
        if (element->caret_position > element->text.size()) {
            element->caret_position = element->text.size();
        }
        focus_map[window] = UiFocus{canvas_entity, element};
    } else {
        focus_map.erase(window);
    }
}

Element* focused_element(ecs::World& world, WindowId window) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end()) {
        return nullptr;
    }
    return it->second.element;
}

void handle_text_input(ecs::World& world, std::string_view text, WindowId window) {
    if (text.empty()) {
        return;
    }
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end() || it->second.element == nullptr) {
        return;
    }
    Element* element = it->second.element;
    if (element->disabled || element->kind != ElementKind::TextInput) {
        return;
    }

    if (element->caret_position > element->text.size()) {
        element->caret_position = element->text.size();
    }
    if (element->selection_anchor && *element->selection_anchor != element->caret_position) {
        const std::size_t start = std::min(*element->selection_anchor, element->caret_position);
        const std::size_t end = std::max(*element->selection_anchor, element->caret_position);
        element->text.erase(start, end - start);
        element->caret_position = start;
    }
    // Typing always ends selection tracking — including a stale anchor left equal to
    // caret_position by a click/Ctrl+A that was never followed by an actual drag/Shift-extend;
    // left set, it would wrongly reappear as a "selection" the moment caret_position next moves
    // away from it by some other means.
    element->selection_anchor.reset();
    element->text.insert(element->caret_position, text);
    element->caret_position += text.size();
    element->caret_blink_timer = 0.0f;

    write_text_binding(world, it->second.canvas_entity, element);
}

void handle_key(ecs::World& world, KeyCode key, bool down, bool /*repeat*/, WindowId window) {
    // Ctrl/Shift are tracked here, not in InputSystem/KeyEvent, and on both down and up so a
    // release is never missed — an element losing focus (or nothing ever being focused) while
    // Ctrl is held must not leave UiModifierState stuck reporting it held forever.
    if (key == KeyCode::LCtrl || key == KeyCode::RCtrl) {
        world.ctx<UiModifierState>().modifiers[window].ctrl = down;
        return;
    }
    if (key == KeyCode::LShift || key == KeyCode::RShift) {
        world.ctx<UiModifierState>().modifiers[window].shift = down;
        return;
    }
    if (!down) {
        return;
    }
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end() || it->second.element == nullptr) {
        return;
    }
    Element* element = it->second.element;
    if (element->disabled) {
        return;
    }

    if (element->caret_position > element->text.size()) {
        element->caret_position = element->text.size();
    }

    if (key == KeyCode::Escape) {
        clear_focus(world, window);
        return;
    }

    if (key == KeyCode::Return) {
        UiCanvas* canvas = world.try_get<UiCanvas>(it->second.canvas_entity);
        ViewModel* target = nullptr;
        if (canvas != nullptr && canvas->data_context) {
            target = element->generated_owner != nullptr
                    ? static_cast<ViewModel*>(const_cast<void*>(element->generated_owner))
                    : canvas->data_context.get();
        }
        if (element->kind == ElementKind::Checkbox) {
            element->checked = !element->checked;
            if (is_bound(element->checked_binding) && target != nullptr) {
                target->write_property_float(element->checked_binding, element->checked ? 1.0f : 0.0f);
            }
        }
        ICommand* command = element->command;
        if (command == nullptr && is_bound(element->command_binding) && target != nullptr) {
            command = target->find_command(element->command_binding);
        }
        if (command != nullptr && command->can_execute()) {
            command->execute();
        }
        return;
    }

    const bool ctrl = world.ctx<UiModifierState>().modifiers[window].ctrl;
    const bool shift = world.ctx<UiModifierState>().modifiers[window].shift;
    // A selectable Label copies and moves the caret, but never inserts, deletes, cuts, or pastes.
    const bool editable = element->kind == ElementKind::TextInput;
    // The real (non-collapsed) selection right now, if any — [selection_start, selection_end).
    // Frozen for the rest of this call; every branch below either reads it or ends by `return`ing
    // (Ctrl+A/C/X/V) before it would go stale.
    const bool has_selection = element->selection_anchor && *element->selection_anchor != element->caret_position;
    const std::size_t selection_start =
            has_selection ? std::min(*element->selection_anchor, element->caret_position) : 0;
    const std::size_t selection_end =
            has_selection ? std::max(*element->selection_anchor, element->caret_position) : 0;

    if (ctrl && key == KeyCode::A) {
        element->selection_anchor = 0;
        element->caret_position = element->text.size();
        element->caret_blink_timer = 0.0f;
        return;
    }

    if (ctrl && key == KeyCode::C) {
        if (has_selection && element->allow_copy) {
            UiClipboard& clipboard = world.ctx<UiClipboard>();
            if (clipboard.set_text) {
                clipboard.set_text(element->text.substr(selection_start, selection_end - selection_start));
            }
        }
        return;
    }

    // Cut is copy-then-delete: it needs allow_copy (read permission), not allow_paste.
    // A Label has nothing to delete, so the whole chord is ignored (the clipboard stays put).
    if (editable && ctrl && key == KeyCode::X) {
        if (has_selection && element->allow_copy) {
            UiClipboard& clipboard = world.ctx<UiClipboard>();
            if (clipboard.set_text) {
                clipboard.set_text(element->text.substr(selection_start, selection_end - selection_start));
            }
            element->text.erase(selection_start, selection_end - selection_start);
            element->caret_position = selection_start;
            element->selection_anchor.reset();
            element->caret_blink_timer = 0.0f;
            write_text_binding(world, it->second.canvas_entity, element);
        }
        return;
    }

    if (editable && ctrl && key == KeyCode::V) {
        if (element->allow_paste) {
            UiClipboard& clipboard = world.ctx<UiClipboard>();
            if (clipboard.get_text) {
                if (const std::optional<std::string> pasted = clipboard.get_text(); pasted.has_value()) {
                    std::size_t insert_at = element->caret_position;
                    if (has_selection) {
                        element->text.erase(selection_start, selection_end - selection_start);
                        insert_at = selection_start;
                    }
                    // Unconditionally, same reasoning as Backspace/Delete above: a stale anchor
                    // left equal to insert_at must not resurface as a "selection" later.
                    element->selection_anchor.reset();
                    element->text.insert(insert_at, *pasted);
                    element->caret_position = insert_at + pasted->size();
                    element->caret_blink_timer = 0.0f;
                    write_text_binding(world, it->second.canvas_entity, element);
                }
            }
        }
        return;
    }

    // Backspace/Delete replace the whole selection (if any) instead of one character. Left/Right/
    // Home/End: with Shift held, arm selection_anchor from the pre-move caret (if not already
    // armed) and extend; without Shift, an existing selection collapses to the edge the key points
    // toward (Left/Home -> start, Right/End -> end) instead of moving by one more character —
    // matching every other text editor's convention.
    bool text_changed = false;
    if (!editable && (key == KeyCode::Backspace || key == KeyCode::Delete)) {
        return;
    }
    if (key == KeyCode::Backspace) {
        // Reset unconditionally, not only in the has_selection branch: left set, a stale anchor
        // equal to the pre-erase caret_position would wrongly reappear as a "selection" once the
        // single-char erase below moves caret_position away from it.
        element->selection_anchor.reset();
        if (has_selection) {
            element->text.erase(selection_start, selection_end - selection_start);
            element->caret_position = selection_start;
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        } else if (element->caret_position > 0) {
            const std::size_t prev = prev_utf8_char(element->text, element->caret_position);
            element->text.erase(prev, element->caret_position - prev);
            element->caret_position = prev;
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        }
    } else if (key == KeyCode::Delete) {
        element->selection_anchor.reset();
        if (has_selection) {
            element->text.erase(selection_start, selection_end - selection_start);
            element->caret_position = selection_start;
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        } else if (element->caret_position < element->text.size()) {
            const std::size_t next = next_utf8_char(element->text, element->caret_position);
            element->text.erase(element->caret_position, next - element->caret_position);
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        }
    } else if (key == KeyCode::Left) {
        if (shift && !element->selection_anchor) {
            element->selection_anchor = element->caret_position;
        }
        if (!shift && has_selection) {
            element->caret_position = selection_start;
        } else if (const std::optional<std::size_t> jumped =
                           element->kind == ElementKind::Label
                                   ? formula_step(element->text, element->caret_position, true)
                                   : std::nullopt) {
            element->caret_position = *jumped;
        } else {
            element->caret_position = prev_utf8_char(element->text, element->caret_position);
        }
        if (!shift) {
            element->selection_anchor.reset();
        }
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::Right) {
        if (shift && !element->selection_anchor) {
            element->selection_anchor = element->caret_position;
        }
        if (!shift && has_selection) {
            element->caret_position = selection_end;
        } else if (const std::optional<std::size_t> jumped =
                           element->kind == ElementKind::Label
                                   ? formula_step(element->text, element->caret_position, false)
                                   : std::nullopt) {
            element->caret_position = *jumped;
        } else {
            element->caret_position = next_utf8_char(element->text, element->caret_position);
        }
        if (!shift) {
            element->selection_anchor.reset();
        }
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::Home) {
        if (shift && !element->selection_anchor) {
            element->selection_anchor = element->caret_position;
        } else if (!shift) {
            element->selection_anchor.reset();
        }
        element->caret_position = 0;
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::End) {
        if (shift && !element->selection_anchor) {
            element->selection_anchor = element->caret_position;
        } else if (!shift) {
            element->selection_anchor.reset();
        }
        element->caret_position = element->text.size();
        element->caret_blink_timer = 0.0f;
    }

    if (text_changed) {
        write_text_binding(world, it->second.canvas_entity, element);
    }
}

}
