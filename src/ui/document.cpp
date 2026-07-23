#include <engine/ui/document.h>

#include "inline_math.h"
#include "math/math_element.h"
#include "painter.h"
#include "popup.h"
#include "ui/text_select.h"

#include <engine/loc/catalog.h>
#include <engine/ui/canvas.h> // rect_contains

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ui {

    // -1 keys `line-height: normal` (the font metric stays on the block). Any other value is the explicit
    // stride in design px. Parsed explicit values are > 0, so they do not collide with this sentinel.
    constexpr float kLineHeightNormalKey = -1.0f;

    [[nodiscard]] static float line_height_cache_key(const Element &element, float font_size) {
        if (element.line_height.kind == LineHeightKind::Normal) {
            return kLineHeightNormalKey;
        }
        return resolve_line_height(element.line_height, font_size, 0.0f);
    }

    // Replaces the font metric on `block` when `line-height` is set. Normal leaves the metric in place.
    static void apply_used_line_height(const Element &element, float font_size, TextBlock &block) {
        if (element.line_height.kind == LineHeightKind::Normal) {
            return;
        }
        const float used = resolve_line_height(element.line_height, font_size, block.line_height);
        if (used > 0.0f) {
            block.line_height = used;
        }
    }

    namespace {

        constexpr float kDefaultImageSize = 32.0f;
        constexpr float kDefaultCheckboxSize = 20.0f;
        // avail_x for a box whose container has no definite width (a hug-sized ancestor): nothing to wrap against.
        constexpr float kUnboundedWidth = std::numeric_limits<float>::infinity();

        void layout_element(Element &element, const render::Rect &box, IUiPainter *painter, glm::vec2 parent_content,
                            const render::Rect &containing_block, bool partial);
        void layout_absolute(Element &element, const render::Rect &containing_block, IUiPainter *painter, bool partial);
        void translate_layout(Element &element, float dx, float dy, const render::Rect &containing_block,
                              IUiPainter *painter);
        [[nodiscard]] glm::vec2 compute_used(const Element &element, IUiPainter *painter, glm::vec2 parent_content,
                                             float avail_x);

        // position: relative never reflows siblings (they already packed/cursor'd against the
        // pre-offset size) - it only nudges this element's own already-placed layout_rect.
        void apply_relative_offset(Element &element, glm::vec2 basis, float em_basis) {
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
            std::optional<float> max_width;
            std::optional<float> min_height;
            float font_size = kDefaultFontSize;
        };

        [[nodiscard]] ResolvedBox resolve_box(const Element &element, glm::vec2 parent_content) {
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
            if (element.max_width) {
                box.max_width = resolve_length(*element.max_width, parent_content.x, box.font_size);
            }
            if (element.min_height) {
                box.min_height = resolve_length(*element.min_height, parent_content.y, box.font_size);
            }
            return box;
        }

        [[nodiscard]] glm::vec2 content_basis(const ResolvedBox &box) {
            return {
                    box.width ? std::max(0.0f, *box.width - box.padding.left - box.padding.right) : 0.0f,
                    box.height ? std::max(0.0f, *box.height - box.padding.top - box.padding.bottom) : 0.0f,
            };
        }

        [[nodiscard]] render::Rect inset_rect(const render::Rect &rect, const BoxInsets &padding) {
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

        // Width the element's border box may take. `avail_x` is what its container can give it (kUnboundedWidth when
        // unknown): an explicit width wins, else the container's width less the margins, then max-width caps it and
        // min-width lifts it (CSS order). Infinite when nothing bounds it.
        [[nodiscard]] float outer_width_limit(const ResolvedBox &box, float avail_x) {
            float outer = box.width ? *box.width : avail_x - box.margin.left - box.margin.right;
            if (box.max_width) {
                outer = std::min(outer, *box.max_width);
            }
            if (box.min_width) {
                outer = std::max(outer, *box.min_width);
            }
            return std::max(0.0f, outer);
        }

        // Width left for the element's content (what text wraps at, and what its children may use).
        [[nodiscard]] float content_width_limit(const ResolvedBox &box, float avail_x) {
            return std::max(0.0f, outer_width_limit(box, avail_x) - box.padding.left - box.padding.right);
        }

        // Memoized on `element` (see Element::text_measure_cache_* in document.h): a reconciled
        // ItemsControl-generated Element keeps its identity across frames, so an unchanged row's text is
        // shaped once instead of every layout(). Only the real-painter path is cached — the
        // painter-less fallback is a cheap approximation and must never be served from (or poison) the
        // real-painter cache, since the same Element can be laid out both ways across its lifetime (e.g.
        // tests calling the no-painter layout() overload).
        [[nodiscard]] glm::vec2 measure_element_text(const Element &element, IUiPainter *painter, float font_size) {
            if (painter != nullptr) {
                if (element.text_measure_cache_valid && element.text_measure_cache_text == element.text &&
                    element.text_measure_cache_font_family == element.font_family &&
                    element.text_measure_cache_font_size == font_size) {
                    return element.text_measure_cache_result;
                }
                const glm::vec2 result = painter->measure_text(element.text, element.font_family, font_size);
                element.text_measure_cache_text = element.text;
                element.text_measure_cache_font_family = element.font_family;
                element.text_measure_cache_font_size = font_size;
                element.text_measure_cache_result = result;
                element.text_measure_cache_valid = true;
                return result;
            }
            return fallback_measure_text(element.text, font_size);
        }

        // Label/Button text size when it may wrap (`white-space: normal`) at `wrap_width` (kUnboundedWidth = never by
        // width). Text that fits on one line and has no newline takes the single-line path above, so unwrapped labels
        // cost what they always did. Otherwise the rows come from IUiPainter::break_lines, memoized on `element` per
        // width and per resolved line-height; an explicit `line-height` replaces the font's row stride. The
        // painter-less layout breaks with the same rules over the rough per-character width and uses font-size as that
        // stride when normal.
        [[nodiscard]] glm::vec2 measure_element_text(const Element &element, IUiPainter *painter, float font_size,
                                                     float wrap_width) {
            // `\(...\)` in a Label/Button is inline math, not characters of the string. Plain text (no delimiter)
            // stays on the path below, including its single-line fast path.
            if (text_has_inline_markup(element.text)) {
                return measure_label_inline(element, painter, font_size, wrap_width);
            }
            const glm::vec2 single = measure_element_text(element, painter, font_size);
            if (element.white_space != WhiteSpace::Normal || element.text.empty()) {
                return single;
            }
            if (single.x <= wrap_width && element.text.find('\n') == std::string::npos) {
                return single;
            }
            const float stride_key = line_height_cache_key(element, font_size);
            const auto size_of = [](const TextBlock &block, glm::vec2 fallback) {
                if (block.lines.empty()) {
                    return fallback;
                }
                float widest = 0.0f;
                for (const TextLine &line: block.lines) {
                    widest = std::max(widest, line.width);
                }
                return glm::vec2{widest, static_cast<float>(block.lines.size()) * block.line_height};
            };
            if (painter == nullptr) {
                TextBlock block;
                block.line_height = font_size;
                block.lines = break_text_lines(element.text, wrap_width, [font_size](std::string_view slice) {
                    return fallback_measure_text(slice, font_size).x;
                });
                apply_used_line_height(element, font_size, block);
                return size_of(block, single);
            }
            if (element.text_wrap_cache_valid && element.text_wrap_cache_width == wrap_width &&
                element.text_wrap_cache_font_size == font_size && element.text_wrap_cache_line_height == stride_key &&
                element.text_wrap_cache_font_family == element.font_family &&
                element.text_wrap_cache_text == element.text) {
                return element.text_wrap_cache_result;
            }
            TextBlock block = painter->break_lines(element.text, element.font_family, font_size, wrap_width);
            apply_used_line_height(element, font_size, block);
            const glm::vec2 result = size_of(block, single);
            element.text_wrap_cache_text = element.text;
            element.text_wrap_cache_font_family = element.font_family;
            element.text_wrap_cache_font_size = font_size;
            element.text_wrap_cache_line_height = stride_key;
            element.text_wrap_cache_width = wrap_width;
            element.text_wrap_cache_block = std::move(block);
            element.text_wrap_cache_result = result;
            element.text_wrap_cache_valid = true;
            return result;
        }

        // A Math element's formula size. Needs the painter's math font (glyph metrics, no GPU); without one — the
        // painter-less layout used by tests, or a window whose font has not been registered yet — it is a rough
        // text-like size, and the layout dirty-gate (UiDocument::last_layout_math_font) relays out once the font
        // arrives.
        [[nodiscard]] glm::vec2 measure_math(const Element &element, IUiPainter *painter, float font_size) {
            if (painter != nullptr) {
                if (const math::MathFont *font = painter->math_font()) {
                    const math::MathLayout &layout = math::element_layout(element, *font, font_size);
                    return {layout.width, layout.height()};
                }
            }
            return fallback_measure_text(element.text, font_size);
        }

        // CSS order: max-width caps first, then min-width wins over it.
        [[nodiscard]] float clamp_axis(std::optional<float> specified, std::optional<float> min_size,
                                       std::optional<float> max_size, float hug) {
            float value = specified.value_or(hug);
            if (max_size) {
                value = std::min(value, *max_size);
            }
            if (min_size) {
                value = std::max(value, *min_size);
            }
            return std::max(0.0f, value);
        }

        template<typename ElementT, typename Out>
        void collect_layout_children(ElementT &element, std::vector<Out *> &children) {
            children.clear();
            if (element.kind == ElementKind::ItemsControl) {
                children.reserve(element.generated_items.size());
                for (auto &child: element.generated_items) {
                    if (child.kind != ElementKind::Popup && !child.display_none) {
                        children.push_back(&child);
                    }
                }
                return;
            }
            children.reserve(element.children.size());
            for (auto &child: element.children) {
                if (child.kind != ElementKind::ItemTemplate && child.kind != ElementKind::Popup && !child.display_none) {
                    children.push_back(&child);
                }
            }
        }

        // `avail_x` is the width the element's container can offer it (see outer_width_limit); only wrapping text reads
        // it.
        [[nodiscard]] glm::vec2 intrinsic_size(const Element &element, IUiPainter *painter, glm::vec2 parent_content,
                                               float avail_x) {
            const ResolvedBox box = resolve_box(element, parent_content);
            if (element.kind == ElementKind::Label || element.kind == ElementKind::Button) {
                const glm::vec2 text =
                        measure_element_text(element, painter, box.font_size, content_width_limit(box, avail_x));
                return {
                        box.padding.left + text.x + box.padding.right,
                        box.padding.top + text.y + box.padding.bottom,
                };
            }
            if (element.kind == ElementKind::TextInput) {
                const glm::vec2 text = measure_element_text(element, painter, box.font_size);
                constexpr float kDefaultTextInputWidth = 100.0f;
                constexpr float kDefaultTextInputHeight = 24.0f;
                const float text_w = std::max(text.x, kDefaultTextInputWidth);
                const float text_h = std::max(text.y, kDefaultTextInputHeight);
                return {
                        box.padding.left + text_w + box.padding.right,
                        box.padding.top + text_h + box.padding.bottom,
                };
            }
            if (element.kind == ElementKind::Math) {
                const glm::vec2 formula = measure_math(element, painter, box.font_size);
                return {
                        box.padding.left + formula.x + box.padding.right,
                        box.padding.top + formula.y + box.padding.bottom,
                };
            }
            if (element.kind == ElementKind::Image) {
                return {
                        box.padding.left + kDefaultImageSize + box.padding.right,
                        box.padding.top + kDefaultImageSize + box.padding.bottom,
                };
            }
            if (element.kind == ElementKind::Checkbox) {
                return {
                        box.padding.left + kDefaultCheckboxSize + box.padding.right,
                        box.padding.top + kDefaultCheckboxSize + box.padding.bottom,
                };
            }
            if (!packs_children(element.kind) && element.kind != ElementKind::ItemsControl) {
                return {
                        box.padding.left + box.padding.right,
                        box.padding.top + box.padding.bottom,
                };
            }

            const glm::vec2 child_basis = content_basis(box);
            const float child_avail_x = content_width_limit(box, avail_x);
            std::vector<const Element *> children;
            collect_layout_children(element, children);
            // `position: absolute` is out of flow: it is placed against its containing block and adds nothing to
            // the size its parent hugs.
            std::erase_if(children, [](const Element *child) { return child->position == PositionMode::Absolute; });
            float main = 0.0f;
            float cross = 0.0f;
            for (std::size_t i = 0; i < children.size(); ++i) {
                const Element &child = *children[i];
                const ResolvedBox child_box = resolve_box(child, child_basis);
                const glm::vec2 used = compute_used(child, painter, child_basis, child_avail_x);
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

        glm::vec2 compute_used(const Element &element, IUiPainter *painter, glm::vec2 parent_content, float avail_x) {
            const ResolvedBox box = resolve_box(element, parent_content);
            // Both axes specified: children cannot change the used border box, so don't walk them.
            if (box.width && box.height) {
                return {
                        clamp_axis(box.width, box.min_width, box.max_width, *box.width),
                        clamp_axis(box.height, box.min_height, std::nullopt, *box.height),
                };
            }
            const glm::vec2 hug = intrinsic_size(element, painter, parent_content, avail_x);
            return {
                    clamp_axis(box.width, box.min_width, box.max_width, hug.x),
                    clamp_axis(box.height, box.min_height, std::nullopt, hug.y),
            };
        }

        // Reuse a clean subtree's last used size while a sibling animates. A basis change (parent content
        // box) invalidates the cache because percentages resolve against it.
        glm::vec2 cached_used(Element &element, IUiPainter *painter, glm::vec2 basis, float avail, bool partial) {
            if (partial && element.layout_used_cache_valid && !element.layout_inputs_changed &&
                !element.layout_descendant_inputs_changed && element.layout_used_basis_w == basis.x &&
                element.layout_used_basis_h == basis.y && element.layout_used_avail_x == avail) {
                return element.layout_used_cache;
            }
            const glm::vec2 used = compute_used(element, painter, basis, avail);
            element.layout_used_cache = used;
            element.layout_used_cache_valid = true;
            element.layout_used_basis_w = basis.x;
            element.layout_used_basis_h = basis.y;
            element.layout_used_avail_x = avail;
            return used;
        }

        // position: absolute is resolved against `containing_block` (the nearest ancestor with
        // position != Static, or the canvas root) rather than packed into the normal flow. Explicit or
        // hug size is used by default; when both opposite insets are set and no explicit size on that
        // axis, the box stretches to fill instead.
        void layout_absolute(Element &element, const render::Rect &containing_block, IUiPainter *painter,
                             bool partial) {
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

            // Shrink-to-fit against the containing block, less whichever insets are set; both insets and no width
            // stretch (below), so text wraps at the stretched width the height is measured for.
            float avail_x = containing_block.w;
            if (left) {
                avail_x -= *left;
            }
            if (right) {
                avail_x -= *right;
            }
            glm::vec2 used = cached_used(element, painter, basis, std::max(0.0f, avail_x), partial);
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

            layout_element(element, render::Rect{x, y, used.x, used.y}, painter, basis, containing_block, partial);
        }

        // A positioned element, or a popup, is the containing block of its `position: absolute` descendants.
        bool establishes_containing_block(const Element &element) {
            return element.position != PositionMode::Static || element.kind == ElementKind::Popup;
        }

        // A Popup is out of flow. It hugs its content with nothing to wrap against (max-width caps it), and
        // percentages resolve against the anchor's content box. Layout leaves it at the anchor's top-left;
        // place_popups (popup.h) works out where it is shown.
        void layout_popups(Element &anchor, glm::vec2 basis, IUiPainter *painter, bool partial) {
            for (Element &child: anchor.children) {
                if (child.kind != ElementKind::Popup || child.display_none) {
                    continue;
                }
                const glm::vec2 used = cached_used(child, painter, basis, kUnboundedWidth, partial);
                const render::Rect box{anchor.layout_rect.x, anchor.layout_rect.y, used.x, used.y};
                layout_element(child, box, painter, basis, box, partial);
            }
        }

        void layout_stack(Element &element, const render::Rect &allocated, IUiPainter *painter, const ResolvedBox &self,
                          const render::Rect &containing_block, bool partial) {
            std::vector<Element *> children;
            collect_layout_children(element, children);
            if (children.empty()) {
                return;
            }

            std::vector<Element *> flow;
            std::vector<Element *> absolute;
            flow.reserve(children.size());
            for (Element *child: children) {
                if (child->position == PositionMode::Absolute) {
                    absolute.push_back(child);
                } else {
                    flow.push_back(child);
                }
            }

            const glm::vec2 child_basis{allocated.w, allocated.h};
            if (!flow.empty()) {
                // Resolve each flow child's box + used size exactly once per layout_stack call and
                // reuse it below for both the packed/cross accumulation and the actual placement —
                // resolve_box/compute_used recurse into the child's own subtree (and, for
                // Label/Button/TextInput, call into the painter's text shaping), so computing them
                // twice here doubles layout cost for every level of nesting.
                struct FlowMetrics {
                    ResolvedBox box;
                    glm::vec2 used;
                };
                std::vector<FlowMetrics> metrics;
                metrics.reserve(flow.size());
                for (Element *child: flow) {
                    metrics.push_back({
                            resolve_box(*child, child_basis),
                            cached_used(*child, painter, child_basis, child_basis.x, partial),
                    });
                }

                float packed = 0.0f;
                float cross = 0.0f;
                for (std::size_t i = 0; i < flow.size(); ++i) {
                    const ResolvedBox &child_box = metrics[i].box;
                    const glm::vec2 &used = metrics[i].used;
                    if (element.direction == StackDirection::Horizontal) {
                        packed += child_box.margin.left + used.x + child_box.margin.right;
                        cross = std::max(cross, child_box.margin.top + used.y + child_box.margin.bottom);
                    } else {
                        packed += child_box.margin.top + used.y + child_box.margin.bottom;
                        cross = std::max(cross, child_box.margin.left + used.x + child_box.margin.right);
                    }
                    if (i + 1 < flow.size()) {
                        packed += self.gap;
                    }
                }

                if (element.direction == StackDirection::Horizontal) {
                    element.max_scroll_x = std::max(0.0f, packed - allocated.w);
                    element.max_scroll_y = std::max(0.0f, cross - allocated.h);
                } else {
                    element.max_scroll_x = std::max(0.0f, cross - allocated.w);
                    element.max_scroll_y = std::max(0.0f, packed - allocated.h);
                }
                element.scroll_x = std::clamp(element.scroll_x, 0.0f, element.max_scroll_x);
                element.scroll_y = std::clamp(element.scroll_y, 0.0f, element.max_scroll_y);

                const bool horizontal = element.direction == StackDirection::Horizontal;
                const float leftover = std::max(0.0f, (horizontal ? allocated.w : allocated.h) - packed);
                float cursor = horizontal ? allocated.x : allocated.y;
                // space-between splits leftover space into the gaps between children instead of around
                // the group (first child flush to the start, last flush to the end) — one fewer gap than
                // children, so a single child has nothing to split against and behaves like Start.
                float justify_gap = 0.0f;
                if (element.justify == UiAlign::Center) {
                    cursor += leftover * 0.5f;
                } else if (element.justify == UiAlign::End) {
                    cursor += leftover;
                } else if (element.justify == UiAlign::SpaceBetween && flow.size() > 1) {
                    justify_gap = leftover / static_cast<float>(flow.size() - 1);
                }

                for (std::size_t i = 0; i < flow.size(); ++i) {
                    Element &child = *flow[i];
                    const ResolvedBox &child_box = metrics[i].box;
                    const glm::vec2 &used = metrics[i].used;
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
                        layout_element(child, render::Rect{cursor, y, used.x, used.y}, painter, child_basis,
                                       containing_block, partial);
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
                        layout_element(child, render::Rect{x, cursor, used.x, used.y}, painter, child_basis,
                                       containing_block, partial);
                        apply_relative_offset(child, child_basis, child_box.font_size);
                        cursor += used.y + child_box.margin.bottom;
                    }
                    if (i + 1 < flow.size()) {
                        cursor += self.gap + justify_gap;
                    }
                }
            }

            for (Element *child: absolute) {
                layout_absolute(*child, containing_block, painter, partial);
            }
        }

        void translate_layout(Element &element, float dx, float dy, const render::Rect &containing_block,
                              IUiPainter *painter) {
            if (dx == 0.0f && dy == 0.0f) {
                return;
            }
            element.layout_rect.x += dx;
            element.layout_rect.y += dy;
            // A positioned element is the containing block of its absolute descendants. A static one is
            // not: those absolutes stay against the block that was passed in, so they are placed again
            // instead of being slid with the flow.
            const render::Rect child_block = establishes_containing_block(element) ? element.layout_rect : containing_block;
            const auto translate_child = [&](Element &child) {
                if (child.kind == ElementKind::ItemTemplate || child.display_none) {
                    return;
                }
                if (child.position == PositionMode::Absolute) {
                    layout_absolute(child, child_block, painter, true);
                } else {
                    translate_layout(child, dx, dy, child_block, painter);
                }
            };
            for (Element &child: element.children) {
                translate_child(child);
            }
            for (Element &child: element.generated_items) {
                translate_child(child);
            }
        }

        void layout_element(Element &element, const render::Rect &box, IUiPainter *painter, glm::vec2 parent_content,
                            const render::Rect &containing_block, bool partial) {
            if (element.kind == ElementKind::ItemTemplate) {
                element.layout_rect = box;
                return;
            }
            // Unchanged input and unchanged box: keep every descendant rect. Same size, new origin: slide
            // the flow subtree. A size change falls through and lays the subtree out again.
            if (partial && !element.layout_inputs_changed && !element.layout_descendant_inputs_changed) {
                if (element.layout_rect.x == box.x && element.layout_rect.y == box.y &&
                    element.layout_rect.w == box.w && element.layout_rect.h == box.h) {
                    return;
                }
                if (element.layout_rect.w == box.w && element.layout_rect.h == box.h) {
                    translate_layout(element, box.x - element.layout_rect.x, box.y - element.layout_rect.y,
                                     containing_block, painter);
                    return;
                }
            }
            element.layout_rect = box;
            const ResolvedBox resolved = resolve_box(element, parent_content);
            const render::Rect content = inset_rect(box, resolved.padding);
            const glm::vec2 child_basis{content.w, content.h};
            // A positioned element (relative or absolute) becomes the containing block its own
            // descendants resolve `position: absolute` against.
            const render::Rect child_containing_block = establishes_containing_block(element) ? box : containing_block;
            if (packs_children(element.kind) || element.kind == ElementKind::ItemsControl) {
                layout_stack(element, content, painter, resolved, child_containing_block, partial);
                layout_popups(element, child_basis, painter, partial);
                return;
            }

            std::vector<Element *> flow;
            std::vector<Element *> absolute;
            for (Element &child: element.children) {
                if (child.kind == ElementKind::ItemTemplate || child.kind == ElementKind::Popup || child.display_none) {
                    continue;
                }
                if (child.position == PositionMode::Absolute) {
                    absolute.push_back(&child);
                } else {
                    flow.push_back(&child);
                }
            }
            for (Element *child_ptr: flow) {
                Element &child = *child_ptr;
                const ResolvedBox child_box = resolve_box(child, child_basis);
                const glm::vec2 used = cached_used(child, painter, child_basis, child_basis.x, partial);
                layout_element(child,
                               render::Rect{content.x + child_box.margin.left, content.y + child_box.margin.top, used.x,
                                            used.y},
                               painter, child_basis, child_containing_block, partial);
                apply_relative_offset(child, child_basis, child_box.font_size);
            }
            for (Element *child_ptr: absolute) {
                layout_absolute(*child_ptr, child_containing_block, painter, partial);
            }
            layout_popups(element, child_basis, painter, partial);
        }

        std::int64_t tr_number(float n) {
            if (!std::isfinite(n)) {
                return 0;
            }
            // 2^53 is a power of two, so float can hold it, and it still fits in int64. Counts past it are
            // not meaningful plurals; clamping keeps the cast defined.
            constexpr float kLimit = 9007199254740992.f;
            if (n >= kLimit) {
                return static_cast<std::int64_t>(kLimit);
            }
            if (n <= -kLimit) {
                return -static_cast<std::int64_t>(kLimit);
            }
            return static_cast<std::int64_t>(n);
        }

        std::expected<void, UiError> bind_element(Element &element, ViewModel &vm, IFatalError *fatal, bool in_template,
                                                  const Element *scroll_context, const engine::loc::Catalog *catalog) {
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
            if (auto result = require_property(element.pan_x_binding); !result) {
                return result;
            }
            if (auto result = require_property(element.pan_y_binding); !result) {
                return result;
            }
            if (auto result = require_property(element.zoom_binding); !result) {
                return result;
            }
            if (auto result = require_property(element.checked_binding); !result) {
                return result;
            }
            if (auto result = require_property(element.open_binding); !result) {
                return result;
            }
            for (const CustomPropertyBinding &custom: element.custom_property_bindings) {
                if (auto result = require_property(custom.binding); !result) {
                    return result;
                }
            }
            if (!in_template) {
                for (const TrArg &arg: element.tr_args) {
                    if (auto result = require_property(arg.binding); !result) {
                        return result;
                    }
                }
            }

            if (is_bound(element.command_binding) && !in_template) {
                ICommand *command = vm.find_command(element.command_binding);
                if (command == nullptr) {
                    if (fatal != nullptr) {
                        fatal->report("UI binding name is not registered");
                    }
                    return std::unexpected(UiError::MissingBinding);
                }
                element.command = command;
                // A TextInput commonly carries a command only for Enter-to-submit (canvas.cpp handle_key's
                // Return case). Disabling the field whenever that command cannot execute yet (for example
                // because this same field is still empty) makes it permanently untypeable: handle_text_input
                // and handle_key both bail out on a disabled element, so it can never receive the keystroke
                // that would make the command executable. Buttons still grey out; only TextInput is exempt.
                if (element.kind != ElementKind::TextInput) {
                    element.disabled = !command->can_execute();
                }
            }

            if (is_bound(element.paint_binding) && !in_template) {
                IPaint *paint = vm.find_paint(element.paint_binding);
                if (paint == nullptr) {
                    if (fatal != nullptr) {
                        fatal->report("UI binding name is not registered");
                    }
                    return std::unexpected(UiError::MissingBinding);
                }
                element.paint = paint;
            }

            if (is_bound(element.text_binding)) {
                (void) vm.assign_property_string(element.text_binding, element.text);
            }
            if (is_bound(element.content_binding)) {
                (void) vm.assign_property_string(element.content_binding, element.text);
            }
            if (!element.tr_key.empty() && !in_template) {
                if (catalog == nullptr) {
                    if (element.text != element.tr_key) {
                        element.text = element.tr_key;
                    }
                    if (fatal != nullptr) {
                        fatal->report("missing string key \"" + element.tr_key + "\"");
                        return std::unexpected(UiError::MissingString);
                    }
                } else {
                    std::vector<std::string> held;
                    std::vector<engine::loc::Arg> args;
                    held.reserve(element.tr_args.size());
                    args.reserve(element.tr_args.size());
                    for (const TrArg &arg: element.tr_args) {
                        if (const auto number = vm.read_property_float(arg.binding)) {
                            args.push_back(engine::loc::Arg{arg.name, tr_number(*number)});
                        } else if (auto text = vm.read_property_string(arg.binding)) {
                            held.push_back(std::move(*text));
                            args.push_back(engine::loc::Arg{arg.name, std::string_view{held.back()}});
                        }
                    }
                    engine::loc::Translated translated = catalog->text(element.tr_key, args);
                    if (element.text != translated.text) {
                        element.text = std::move(translated.text);
                    }
                    if (translated.missing_from_source && fatal != nullptr) {
                        fatal->report("missing string key \"" + element.tr_key + "\"");
                        return std::unexpected(UiError::MissingString);
                    }
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
            if (is_bound(element.pan_x_binding)) {
                if (auto value = vm.read_property_float(element.pan_x_binding)) {
                    element.pan_x = *value;
                }
            }
            if (is_bound(element.pan_y_binding)) {
                if (auto value = vm.read_property_float(element.pan_y_binding)) {
                    element.pan_y = *value;
                }
            }
            if (is_bound(element.zoom_binding)) {
                if (auto value = vm.read_property_float(element.zoom_binding)) {
                    element.zoom = *value > 0.0f ? std::clamp(*value, kViewportMinZoom, kViewportMaxZoom) : 1.0f;
                }
            }
            // Clamped to the extent the last layout found, as layout clamps it again: ItemsControl virtualization
            // reads scroll_y before this frame's layout, and a view-model that asks for "the end" with a value past
            // it would otherwise generate a window of rows past the last one. The view-model keeps its value, so a
            // list that grew is clamped to its new extent on the next frame.
            if (is_bound(element.scroll_x_binding)) {
                if (auto value = vm.read_property_float(element.scroll_x_binding)) {
                    element.scroll_x = std::clamp(*value, 0.0f, element.max_scroll_x);
                }
            }
            if (is_bound(element.scroll_y_binding)) {
                if (auto value = vm.read_property_float(element.scroll_y_binding)) {
                    element.scroll_y = std::clamp(*value, 0.0f, element.max_scroll_y);
                }
            }
            if (is_bound(element.checked_binding)) {
                if (auto value = vm.read_property_float(element.checked_binding)) {
                    element.checked = *value != 0.0f;
                }
            }
            if (is_bound(element.open_binding)) {
                if (auto value = vm.read_property_float(element.open_binding)) {
                    element.open = *value != 0.0f;
                }
            }
            for (const CustomPropertyBinding &custom: element.custom_property_bindings) {
                const auto existing = element.custom_properties.find(custom.name);
                if (existing == element.custom_properties.end()) {
                    std::string value;
                    if (vm.assign_property_string(custom.binding, value)) {
                        element.custom_properties.emplace(custom.name, std::move(value));
                    }
                } else if (!vm.assign_property_string(custom.binding, existing->second)) {
                    element.custom_properties.erase(existing);
                }
            }

            const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
            const Element *child_scroll_context = is_scrollable_y(element) ? &element : scroll_context;
            for (Element &child: element.children) {
                if (element.generated_owner != nullptr && child.generated_owner == nullptr) {
                    child.generated_owner = element.generated_owner;
                }
                if (auto result = bind_element(child, vm, fatal, nested_template, child_scroll_context, catalog);
                    !result) {
                    return result;
                }
            }

            if (element.kind == ElementKind::ItemsControl && is_bound(element.items_source_binding) && !in_template) {
                const Element *tmpl = nullptr;
                for (const Element &child: element.children) {
                    if (child.kind == ElementKind::ItemTemplate) {
                        tmpl = &child;
                        break;
                    }
                }
                if (tmpl != nullptr) {
                    const std::size_t expected_count = tmpl->children.empty() ? 1 : tmpl->children.size();

                    // --- Virtualization eligibility -------------------------------------------------
                    // Generate only the visible window of rows (+overscan) plus up to two spacer
                    // Elements standing in for the scrolled-past rows, instead of one (or
                    // `expected_count`) Element per item for every item in items_source — see
                    // docs/tech/modules/UI.md's ItemsControl section. Every condition below must hold or
                    // this frame falls back to `window_first = 0, window_last = items.size() - 1` with no
                    // spacers: the historical, byte-identical full generation.
                    constexpr int kVirtualizationOverscanRows = 2;
                    // The scrolled viewport this ItemsControl's rows are windowed against: itself when
                    // it is directly scrollable (the wind-127 self-scrolling case), otherwise the
                    // nearest scrollable ancestor threaded down through `scroll_context` (e.g. an
                    // enclosing <ScrollView> that isn't this control's direct parent). `direction ==
                    // Vertical` is required on the ancestor too: scroll_y / layout_rect.y only line up
                    // with the row axis when the scrolling container also stacks vertically. This is a
                    // strict generalization of the Case A check below (Case A has
                    // effective_context == &element, so the two conditions coincide there) — element's
                    // own `direction == Vertical` is still checked independently right below, since that
                    // condition is about the ItemsControl itself, not its scroll ancestor.
                    const Element *effective_context = is_scrollable_y(element) ? &element : scroll_context;
                    const bool has_scroll_context = effective_context != nullptr &&
                                                    effective_context->direction == StackDirection::Vertical &&
                                                    effective_context->layout_rect.h > 0.0f;
                    std::optional<float> row_height_px;
                    if (!element.suppress_item_virtualization && expected_count == 1 &&
                        element.direction == StackDirection::Vertical && has_scroll_context &&
                        element.gap.unit == LengthUnit::Px && element.gap.calc.empty()) {
                        // Row height can't be read off the static ItemTemplate: apply_layout_style
                        // (paint.cpp) never visits an ItemTemplate's own children, so tmpl's root never
                        // gets a resolved `height` there — only a *generated* clone does, once
                        // apply_layout_style has walked it on a previous frame. Sample the first
                        // surviving real (non-spacer) row from last frame's generated_items and, when
                        // found, treat it as this frame's source of truth and refresh
                        // `virtualization_row_height_cache` with it. When last frame's window held no
                        // real row at all — every generated item was a spacer, or there were none yet —
                        // fall back to the cached value from whenever it was last sampled, instead of
                        // declaring row height unknown: a wrapping ScrollView can scroll this
                        // ItemsControl entirely out of view for several frames in a row (visible window
                        // = zero real rows, one spacer standing in for all of them), and a live sample
                        // alone would never find anything then, tipping eligibility over to full
                        // generation for as long as the control stays offscreen. Only when neither a
                        // live sample nor a prior cached value exists (first bind ever for this control)
                        // does row height stay unknown this frame — self-correcting next frame, same as
                        // the max_scroll_y / layout_rect.h staleness this eligibility check already
                        // relies on above.
                        for (const Element &old: element.generated_items) {
                            if (old.generated_owner != nullptr && !old.display_none && old.height &&
                                old.height->unit == LengthUnit::Px && old.height->calc.empty() &&
                                old.height->value > 0.0f) {
                                row_height_px = old.height->value;
                                element.virtualization_row_height_cache = row_height_px;
                                break;
                            }
                        }
                        if (!row_height_px && element.virtualization_row_height_cache) {
                            row_height_px = element.virtualization_row_height_cache;
                        }
                    }

                    // Decide the window before moving any Element. When generated_items is already that
                    // window, the rows are bound in place. A different set still reconciles by
                    // ViewModel* below, so a surviving row keeps its motion clocks.
                    const std::vector<ViewModel *> items = vm.read_item_source(element.items_source_binding);
                    std::size_t window_first = 0;
                    std::size_t window_last = items.empty() ? 0 : items.size() - 1;
                    float leading_spacer_h = 0.0f;
                    float trailing_spacer_h = 0.0f;
                    // True when the visible row window is empty: the ItemsControl sits entirely outside
                    // effective_context's viewport (Case B only — see below). When true, no real rows
                    // are generated this frame at all, just one spacer standing in for every item.
                    bool window_empty = false;
                    if (row_height_px && !items.empty()) {
                        const float gap_px = element.gap.value;
                        const float row_stride = *row_height_px + gap_px;
                        const auto n = static_cast<std::int64_t>(items.size());

                        // Case A (effective_context == &element, i.e. this ItemsControl scrolls itself):
                        // offset == 0, so scroll_y_effective == element.scroll_y and viewport_h ==
                        // element.layout_rect.h — byte-identical to the wind-127 formula. Case B
                        // (effective_context is an ancestor, not necessarily the direct parent):
                        // layout_rect is always absolute/canvas-space at every nesting depth (each
                        // layout_element/layout_stack positions children from its own already-absolute
                        // content origin), so this one subtraction is correct however many intermediate
                        // Stacks/padding/margins sit between `element` and `effective_context`, without
                        // needing to check whether effective_context is element's direct parent.
                        const float offset = (effective_context == &element)
                                                     ? 0.0f
                                                     : element.layout_rect.y - effective_context->layout_rect.y;
                        const float viewport_h = effective_context->layout_rect.h;
                        const float scroll_y_effective = effective_context->scroll_y - offset;

                        std::int64_t first = static_cast<std::int64_t>(std::floor(scroll_y_effective / row_stride)) -
                                             kVirtualizationOverscanRows;
                        std::int64_t last =
                                static_cast<std::int64_t>(std::ceil((scroll_y_effective + viewport_h) / row_stride)) +
                                kVirtualizationOverscanRows;

                        // layout_stack packs N flow children with exactly (N-1) gaps total (a gap
                        // *between* consecutive children, never a trailing one after the last) — so a
                        // spacer standing in for `count` skipped rows must itself contribute
                        // `count * row_height + (count-1) * gap`, not `count * row_stride`: the gap
                        // between the spacer and its neighboring real row is already accounted for by
                        // layout_stack's own inter-child gap once the spacer takes its place as one flow
                        // child among (spacers + window) total. Getting this wrong would make the
                        // virtualized max_scroll_y drift from the max_scroll_y full generation would
                        // produce by up to 2 * gap.
                        const auto spacer_height = [&](std::size_t count) -> float {
                            if (count == 0) {
                                return 0.0f;
                            }
                            return static_cast<float>(count) * *row_height_px + static_cast<float>(count - 1) * gap_px;
                        };

                        if (last < 0 || first >= n) {
                            // Entirely offscreen: only reachable in Case B, where scroll_y_effective can
                            // legitimately land outside [0, N*row_stride) in either direction (e.g. the
                            // wrapping ScrollView shows content before or after this ItemsControl while
                            // the list itself is fully scrolled past). Case A can never hit this branch:
                            // element.scroll_y is always clamped to [0, element.max_scroll_y] by the
                            // element's own scrolling, which keeps the window over at least one real row.
                            // Collapse to zero real rows plus one spacer covering every item. Critical
                            // invariant (same one wind-127 established for the two-spacer case): the
                            // summed used-height of this ItemsControl (spacers + any real rows) must
                            // always equal N*row_height + (N-1)*gap regardless of window state, because
                            // that sum is exactly what effective_context's own intrinsic_size /
                            // compute_used reads back when laying out content that follows this
                            // ItemsControl (e.g. a footer) and when computing its own max_scroll_y.
                            // spacer_height(N) is that sum by construction for N > 0, so this case is
                            // trivially exact.
                            window_empty = true;
                            window_first = 0;
                            window_last = 0;
                            leading_spacer_h = spacer_height(static_cast<std::size_t>(n));
                            trailing_spacer_h = 0.0f;
                        } else {
                            first = std::clamp<std::int64_t>(first, 0, n - 1);
                            last = std::clamp<std::int64_t>(last, first, n - 1);
                            window_first = static_cast<std::size_t>(first);
                            window_last = static_cast<std::size_t>(last);
                            leading_spacer_h = spacer_height(window_first);
                            trailing_spacer_h = spacer_height(items.size() - 1 - window_last);
                        }
                    }

                    const auto is_spacer = [](const Element &row) {
                        return row.is_virtualization_spacer && row.generated_owner == nullptr;
                    };
                    const auto generated_window_matches = [&] {
                        std::size_t index = 0;
                        const std::vector<Element> &rows = element.generated_items;
                        if (leading_spacer_h > 0.0f) {
                            if (index >= rows.size() || !is_spacer(rows[index])) {
                                return false;
                            }
                            ++index;
                        }
                        if (!window_empty && !items.empty()) {
                            for (std::size_t i = window_first; i <= window_last; ++i) {
                                ViewModel *item = items[i];
                                if (item == nullptr) {
                                    continue;
                                }
                                for (std::size_t n = 0; n < expected_count; ++n) {
                                    if (index >= rows.size() || rows[index].is_virtualization_spacer ||
                                        rows[index].generated_owner != item) {
                                        return false;
                                    }
                                    ++index;
                                }
                            }
                        }
                        if (trailing_spacer_h > 0.0f) {
                            if (index >= rows.size() || !is_spacer(rows[index])) {
                                return false;
                            }
                            ++index;
                        }
                        return index == rows.size();
                    };
                    const auto write_spacer_height = [](Element &spacer, float height) {
                        if (spacer.height && spacer.height->unit == LengthUnit::Px && spacer.height->calc.empty() &&
                            spacer.height->value == height) {
                            return;
                        }
                        spacer.height = Length{height, LengthUnit::Px};
                    };
                    if (generated_window_matches()) {
                        std::size_t index = 0;
                        if (leading_spacer_h > 0.0f) {
                            write_spacer_height(element.generated_items[index++], leading_spacer_h);
                        }
                        if (!window_empty && !items.empty()) {
                            for (std::size_t i = window_first; i <= window_last; ++i) {
                                ViewModel *item = items[i];
                                if (item == nullptr) {
                                    continue;
                                }
                                for (std::size_t n = 0; n < expected_count; ++n) {
                                    if (auto result = bind_element(element.generated_items[index++], *item, fatal,
                                                                   false, child_scroll_context, catalog);
                                        !result) {
                                        return result;
                                    }
                                }
                            }
                        }
                        if (trailing_spacer_h > 0.0f) {
                            write_spacer_height(element.generated_items[index], trailing_spacer_h);
                        }
                    } else {
                        // A changed set. An item still in the window reuses its Element (motion clocks
                        // included). A new item is cloned from the template. Spacers are rebuilt; they
                        // carry no runtime state.
                        std::unordered_map<const void *, std::vector<Element>> previous_by_owner;
                        for (Element &old: element.generated_items) {
                            previous_by_owner[old.generated_owner].push_back(std::move(old));
                        }
                        element.generated_items.clear();

                        if (leading_spacer_h > 0.0f) {
                            Element spacer;
                            spacer.kind = ElementKind::Canvas;
                            spacer.height = Length{leading_spacer_h, LengthUnit::Px};
                            spacer.is_virtualization_spacer = true;
                            element.generated_items.push_back(std::move(spacer));
                        }
                        for (std::size_t i = window_first; !window_empty && !items.empty() && i <= window_last; ++i) {
                            ViewModel *item = items[i];
                            if (item == nullptr) {
                                continue;
                            }
                            if (const auto reused = previous_by_owner.find(item);
                                reused != previous_by_owner.end() && reused->second.size() == expected_count) {
                                for (Element &clone: reused->second) {
                                    if (auto result =
                                                bind_element(clone, *item, fatal, false, child_scroll_context, catalog);
                                        !result) {
                                        return result;
                                    }
                                    element.generated_items.push_back(std::move(clone));
                                }
                                previous_by_owner.erase(reused);
                                continue;
                            }
                            if (tmpl->children.empty()) {
                                Element clone = *tmpl;
                                clone.kind = ElementKind::Stack;
                                clone.children.clear();
                                clone.generated_owner = item;
                                if (auto result =
                                            bind_element(clone, *item, fatal, false, child_scroll_context, catalog);
                                    !result) {
                                    return result;
                                }
                                element.generated_items.push_back(std::move(clone));
                                continue;
                            }
                            for (const Element &node: tmpl->children) {
                                Element clone = node;
                                clone.generated_items.clear();
                                clone.generated_owner = item;
                                if (auto result =
                                            bind_element(clone, *item, fatal, false, child_scroll_context, catalog);
                                    !result) {
                                    return result;
                                }
                                element.generated_items.push_back(std::move(clone));
                            }
                        }
                        if (trailing_spacer_h > 0.0f) {
                            Element spacer;
                            spacer.kind = ElementKind::Canvas;
                            spacer.height = Length{trailing_spacer_h, LengthUnit::Px};
                            spacer.is_virtualization_spacer = true;
                            element.generated_items.push_back(std::move(spacer));
                        }
                    }
                }
            }
            return {};
        }

        const Element *find_by_kind_const(const Element &root, ElementKind kind) {
            if (root.kind == kind) {
                return &root;
            }
            for (const Element &child: root.children) {
                if (const Element *found = find_by_kind_const(child, kind)) {
                    return found;
                }
            }
            return nullptr;
        }

    } // namespace

    // wind-129 layout dirty-gate. See Element's layout_dirty_check_* fields (document.h) for WHY these
    // are the only inputs layout ever depends on. Recurses element.children (skipping ItemTemplate,
    // same as bind_element/layout_element above — a template's own subtree never itself gets laid out)
    // plus element.generated_items (ItemsControl-generated rows/spacers), and for every element visited:
    //   - compares element.text against layout_dirty_check_text
    //   - compares element.custom_properties against layout_dirty_check_custom_properties
    //   - compares the current sequence of generated_items[*].generated_owner against
    //     layout_dirty_check_generated_owners (order and count matter: a reorder or a window shift is
    //     itself a layout-relevant change even when the set of owners is unchanged)
    // then unconditionally overwrites all three cached copies with the current values — regardless of
    // whether this element compared equal — so next frame's comparison is always against the most
    // recent real state, never against a stale "first ever seen" snapshot. The traversal never
    // short-circuits on finding a change: every element's cache must be refreshed every call, so the
    // "changed" result is only ever OR-accumulated into a local, not used to skip visiting the rest of
    // the tree. layout_dirty_check_initialized is false only before the very first call on a given
    // Element (fresh construction or ItemsControl clone), which forces that first call to report
    // "changed" — there is nothing yet to compare against.
    bool layout_state_changed(Element &element) {
        bool changed = false;

        if (!element.layout_dirty_check_initialized || element.text != element.layout_dirty_check_text) {
            changed = true;
        }
        element.layout_dirty_check_text = element.text;

        if (!element.layout_dirty_check_initialized ||
            element.custom_properties != element.layout_dirty_check_custom_properties) {
            changed = true;
        }
        element.layout_dirty_check_custom_properties = element.custom_properties;

        std::vector<const void *> current_owners;
        current_owners.reserve(element.generated_items.size());
        for (const Element &item: element.generated_items) {
            current_owners.push_back(item.generated_owner);
        }
        if (!element.layout_dirty_check_initialized || current_owners != element.layout_dirty_check_generated_owners) {
            changed = true;
        }
        element.layout_dirty_check_generated_owners = std::move(current_owners);

        element.layout_dirty_check_initialized = true;

        for (Element &child: element.children) {
            if (child.kind == ElementKind::ItemTemplate) {
                continue;
            }
            if (layout_state_changed(child)) {
                changed = true;
            }
        }
        for (Element &item: element.generated_items) {
            if (layout_state_changed(item)) {
                changed = true;
            }
        }

        return changed;
    }

    std::expected<void, UiError> apply_bindings(UiDocument &document, ViewModel &data_context, IFatalError *fatal,
                                                const engine::loc::Catalog *catalog) {
        return bind_element(document.root, data_context, fatal, false, nullptr, catalog);
    }

    void layout(UiDocument &document, const render::Rect &canvas_rect, IUiPainter *painter, bool partial) {
        layout_element(document.root, canvas_rect, painter, glm::vec2{canvas_rect.w, canvas_rect.h}, canvas_rect,
                       partial);
    }

    void layout(UiDocument &document, const render::Rect &canvas_rect) { layout(document, canvas_rect, nullptr); }

    const TextBlock *wrapped_text_rows(const Element &element, IUiPainter &painter, float font_size,
                                       float content_width) {
        if (element.white_space != WhiteSpace::Normal || element.text.empty()) {
            return nullptr;
        }
        constexpr float kEpsilon = 0.01f;
        const auto cache_matches_text = [&] {
            return element.text_wrap_cache_valid && element.text_wrap_cache_font_size == font_size &&
                   element.text_wrap_cache_line_height == line_height_cache_key(element, font_size) &&
                   element.text_wrap_cache_font_family == element.font_family &&
                   element.text_wrap_cache_text == element.text;
        };
        if (cache_matches_text() && element.text_wrap_cache_width >= content_width - kEpsilon &&
            element.text_wrap_cache_result.x <= content_width + kEpsilon) {
            return &element.text_wrap_cache_block;
        }
        // Measuring fills the cache when (and only when) the text needs more than one row at this width.
        static_cast<void>(measure_element_text(element, &painter, font_size, content_width));
        if (cache_matches_text() && element.text_wrap_cache_width == content_width) {
            return &element.text_wrap_cache_block;
        }
        return nullptr;
    }

    render::Rect hit_bounds(const Element &element) {
        if (element.rotation_deg == 0.0f && element.scale == 1.0f) {
            return element.layout_rect;
        }
        const render::Rect &rect = element.layout_rect;
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
        for (const glm::vec2 &corner: corners) {
            const float x = corner.x * cos_r - corner.y * sin_r;
            const float y = corner.x * sin_r + corner.y * cos_r;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
        return render::Rect{center.x + min_x, center.y + min_y, max_x - min_x, max_y - min_y};
    }

    std::vector<Element *> child_stacking_order(std::vector<Element> &children) {
        std::vector<Element *> order;
        order.reserve(children.size());
        for (Element &child: children) {
            order.push_back(&child);
        }
        std::stable_sort(order.begin(), order.end(),
                         [](const Element *a, const Element *b) { return a->z_index < b->z_index; });
        return order;
    }

    // An element that shows nothing of its children outside its own box: overflow other than visible, or a Viewport.
    // Its absolute children outside it take no pointer either.
    static bool clips_children(const Element &element) {
        return element.overflow_x != Overflow::Visible || element.overflow_y != Overflow::Visible ||
               element.kind == ElementKind::Viewport;
    }

    // `position: absolute` children of `element`, front to back, that may lie outside its box: out of flow, they are
    // painted wherever they are placed, so they take the pointer there too.
    template<typename Visit>
    static auto visit_absolute_children(Element &element, Visit visit) -> decltype(visit(element)) {
        if (clips_children(element)) {
            return {};
        }
        for (std::vector<Element> *list: {&element.children, &element.generated_items}) {
            std::vector<Element *> order = child_stacking_order(*list);
            for (auto it = order.rbegin(); it != order.rend(); ++it) {
                if ((*it)->kind == ElementKind::Popup || (*it)->position != PositionMode::Absolute) {
                    continue;
                }
                if (auto hit = visit(**it); hit) {
                    return hit;
                }
            }
        }
        return {};
    }

    static Element *hit_test_at(Element &element, float x, float y, bool under_control) {
        if (!element.visible || element.display_none) {
            return nullptr;
        }
        if (!rect_contains(hit_bounds(element), x, y)) {
            const bool child_under_control =
                    under_control || element.kind == ElementKind::Button || element.kind == ElementKind::Checkbox;
            return visit_absolute_children(
                    element, [&](Element &child) { return hit_test_at(child, x, y, child_under_control); });
        }
        if (is_scrollable_y(element)) {
            const render::Rect track = scrollbar_track_rect(element);
            if (track.w > 0.0f && track.h > 0.0f && rect_contains(track, x, y)) {
                return &element;
            }
        }
        if (is_scrollable_x(element)) {
            const render::Rect track = scrollbar_track_rect(element);
            if (track.w > 0.0f && track.h > 0.0f && rect_contains(track, x, y)) {
                return &element;
            }
        }
        float child_x = x;
        float child_y = y;
        if (element.kind == ElementKind::Viewport) {
            const glm::vec2 inverted = inverse_viewport_pointer(element, glm::vec2{x, y});
            child_x = inverted.x;
            child_y = inverted.y;
        } else if (element.scroll_x != 0.0f || element.scroll_y != 0.0f) {
            child_x = x + element.scroll_x;
            child_y = y + element.scroll_y;
        }
        // A Button or Checkbox owns the click, including a Label nested inside it (through a Stack or
        // otherwise). Otherwise `Label { user-select: text }` would steal `<Button><Label/></Button>`.
        const bool child_under_control =
                under_control || element.kind == ElementKind::Button || element.kind == ElementKind::Checkbox;
        // child_stacking_order() is ascending (paint order); iterating its result back-to-front
        // visits the topmost (highest z-index / last-drawn) sibling first.
        // A Popup child is not hit from here: hit_test visits open popups first, from their own offset.
        std::vector<Element *> children = child_stacking_order(element.children);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if ((*it)->kind == ElementKind::Popup) {
                continue;
            }
            if (Element *nested = hit_test_at(**it, child_x, child_y, child_under_control)) {
                return nested;
            }
        }
        std::vector<Element *> generated = child_stacking_order(element.generated_items);
        for (auto it = generated.rbegin(); it != generated.rend(); ++it) {
            if ((*it)->kind == ElementKind::Popup) {
                continue;
            }
            if (Element *nested = hit_test_at(**it, child_x, child_y, child_under_control)) {
                return nested;
            }
        }
        // An open popup owns every point inside it, so a click on its padding does not reach what is under it.
        if (element.kind == ElementKind::Button || element.kind == ElementKind::Checkbox ||
            element.kind == ElementKind::Popup || element.kind == ElementKind::TextInput ||
            element.kind == ElementKind::ScrollView ||
            is_scrollable(element) || is_bound(element.command_binding) || is_bound(element.drag_binding) ||
            has_viewport_camera(element)) {
            return &element;
        }
        if (!under_control && label_text_selectable(element)) {
            return &element;
        }
        return nullptr;
    }

    Element *hit_test(Element &element, float x, float y) {
        if (Element *popup = popup_at(element, x, y)) {
            return hit_test_at(*popup, x - popup->popup_offset.x, y - popup->popup_offset.y, false);
        }
        return hit_test_at(element, x, y, false);
    }

    struct ElementInsets {
        BoxInsets padding{};
        BoxInsets margin{};
    };

    ElementInsets element_insets(const Element &element, glm::vec2 parent_content) {
        const float font = resolve_font_size(element.font_size, parent_content.x);
        return ElementInsets{
                BoxInsets{
                        resolve_length(element.padding.top, parent_content.y, font),
                        resolve_length(element.padding.right, parent_content.x, font),
                        resolve_length(element.padding.bottom, parent_content.y, font),
                        resolve_length(element.padding.left, parent_content.x, font),
                },
                BoxInsets{
                        resolve_length(element.margin.top, parent_content.y, font),
                        resolve_length(element.margin.right, parent_content.x, font),
                        resolve_length(element.margin.bottom, parent_content.y, font),
                        resolve_length(element.margin.left, parent_content.x, font),
                },
        };
    }

    render::Rect expand_rect(const render::Rect &rect, const BoxInsets &margin) {
        return render::Rect{
                rect.x - margin.left,
                rect.y - margin.top,
                std::max(0.0f, rect.w + margin.left + margin.right),
                std::max(0.0f, rect.h + margin.top + margin.bottom),
        };
    }

    LayoutBoxes make_layout_boxes(const Element &element, glm::vec2 parent_content, const SpaceMap &map) {
        const ElementInsets insets = element_insets(element, parent_content);
        const render::Rect border = hit_bounds(element);
        return LayoutBoxes{
                map.apply(expand_rect(border, insets.margin)),
                map.apply(border),
                map.apply(inset_rect(border, insets.padding)),
        };
    }

    glm::vec2 content_size_of(const Element &element, glm::vec2 parent_content) {
        const ElementInsets insets = element_insets(element, parent_content);
        return glm::vec2{
                std::max(0.0f, element.layout_rect.w - insets.padding.left - insets.padding.right),
                std::max(0.0f, element.layout_rect.h - insets.padding.top - insets.padding.bottom),
        };
    }

    SpaceMap child_map_of(const Element &element, const SpaceMap &map) {
        SpaceMap child = map;
        if (element.kind == ElementKind::Viewport) {
            const float zoom = viewport_zoom(element.zoom);
            const glm::vec2 origin{element.layout_rect.x, element.layout_rect.y};
            const glm::vec2 pan{element.pan_x, element.pan_y};
            child.translate += (origin * (1.0f - zoom) + pan * zoom) * map.scale;
            child.scale *= zoom;
        } else if (element.scroll_x != 0.0f || element.scroll_y != 0.0f) {
            child.translate += glm::vec2{-element.scroll_x, -element.scroll_y} * map.scale;
        }
        return child;
    }

    void child_pointer(const Element &element, float x, float y, float &child_x, float &child_y) {
        child_x = x;
        child_y = y;
        if (element.kind == ElementKind::Viewport) {
            const glm::vec2 inverted = inverse_viewport_pointer(element, glm::vec2{x, y});
            child_x = inverted.x;
            child_y = inverted.y;
        } else if (element.scroll_x != 0.0f || element.scroll_y != 0.0f) {
            child_x = x + element.scroll_x;
            child_y = y + element.scroll_y;
        }
    }

    VisualHit hit_visual_at(Element &element, float x, float y, glm::vec2 parent_content, const SpaceMap &map) {
        if (element.kind == ElementKind::ItemTemplate || !element.visible || element.display_none) {
            return {};
        }
        if (!rect_contains(hit_bounds(element), x, y)) {
            const glm::vec2 child_basis = content_size_of(element, parent_content);
            const SpaceMap child_map = child_map_of(element, map);
            const VisualHit hit = visit_absolute_children(element, [&](Element &child) -> std::optional<VisualHit> {
                VisualHit nested = hit_visual_at(child, x, y, child_basis, child_map);
                return nested.element != nullptr ? std::optional<VisualHit>(nested) : std::nullopt;
            }).value_or(VisualHit{});
            return hit;
        }
        if (is_scrollable_y(element) || is_scrollable_x(element)) {
            const render::Rect track = scrollbar_track_rect(element);
            if (track.w > 0.0f && track.h > 0.0f && rect_contains(track, x, y)) {
                return VisualHit{&element, make_layout_boxes(element, parent_content, map)};
            }
        }

        float child_x = x;
        float child_y = y;
        child_pointer(element, x, y, child_x, child_y);
        const SpaceMap child_map = child_map_of(element, map);
        const glm::vec2 child_basis = content_size_of(element, parent_content);

        std::vector<Element *> children = child_stacking_order(element.children);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if ((*it)->kind == ElementKind::Popup) {
                continue;
            }
            if (VisualHit nested = hit_visual_at(**it, child_x, child_y, child_basis, child_map);
                nested.element != nullptr) {
                return nested;
            }
        }
        std::vector<Element *> generated = child_stacking_order(element.generated_items);
        for (auto it = generated.rbegin(); it != generated.rend(); ++it) {
            if ((*it)->kind == ElementKind::Popup) {
                continue;
            }
            if (VisualHit nested = hit_visual_at(**it, child_x, child_y, child_basis, child_map);
                nested.element != nullptr) {
                return nested;
            }
        }
        return VisualHit{&element, make_layout_boxes(element, parent_content, map)};
    }

    bool find_layout_boxes(Element &element, const Element &target, glm::vec2 parent_content, const SpaceMap &map,
                           LayoutBoxes &out) {
        if (&element == &target) {
            out = make_layout_boxes(element, parent_content, map);
            return true;
        }
        const SpaceMap child_map = child_map_of(element, map);
        const glm::vec2 child_basis = content_size_of(element, parent_content);
        // A popup is shown from its own offset, whatever scroll or camera its anchor sits under.
        const auto map_for = [&](const Element &child) {
            return child.kind == ElementKind::Popup ? SpaceMap{1.0f, child.popup_offset} : child_map;
        };
        for (Element &child: element.children) {
            if (find_layout_boxes(child, target, child_basis, map_for(child), out)) {
                return true;
            }
        }
        for (Element &child: element.generated_items) {
            if (find_layout_boxes(child, target, child_basis, map_for(child), out)) {
                return true;
            }
        }
        return false;
    }

    VisualHit hit_test_visual(Element &root, float x, float y) {
        const std::vector<OpenPopup> popups = open_popups(root);
        for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
            Element &popup = *it->popup;
            if (VisualHit hit = hit_visual_at(popup, x - popup.popup_offset.x, y - popup.popup_offset.y,
                                              it->parent_content, SpaceMap{1.0f, popup.popup_offset});
                hit.element != nullptr) {
                return hit;
            }
        }
        return hit_visual_at(root, x, y, glm::vec2{root.layout_rect.w, root.layout_rect.h}, SpaceMap{});
    }

    LayoutBoxes layout_boxes(Element &root, const Element &target) {
        LayoutBoxes boxes;
        find_layout_boxes(root, target, glm::vec2{root.layout_rect.w, root.layout_rect.h}, SpaceMap{}, boxes);
        return boxes;
    }

    void find_viewport_at_impl(Element &element, float x, float y, Element *&found) {
        if (element.kind == ElementKind::ItemTemplate || !element.visible || element.display_none) {
            return;
        }
        float child_x = x;
        float child_y = y;
        if (element.kind == ElementKind::Viewport) {
            if (!rect_contains(element.layout_rect, x, y)) {
                return;
            }
            found = &element;
            const glm::vec2 inverted = inverse_viewport_pointer(element, glm::vec2{x, y});
            child_x = inverted.x;
            child_y = inverted.y;
        } else if (element.scroll_x != 0.0f || element.scroll_y != 0.0f) {
            child_x = x + element.scroll_x;
            child_y = y + element.scroll_y;
        }
        for (Element *child: child_stacking_order(element.children)) {
            if (child->kind != ElementKind::Popup) {
                find_viewport_at_impl(*child, child_x, child_y, found);
            }
        }
        for (Element *child: child_stacking_order(element.generated_items)) {
            if (child->kind != ElementKind::Popup) {
                find_viewport_at_impl(*child, child_x, child_y, found);
            }
        }
    }

    Element *find_viewport_at(Element &root, float x, float y) {
        Element *found = nullptr;
        // Over an open popup only that popup is searched: nothing under it scrolls or zooms.
        if (Element *popup = popup_at(root, x, y)) {
            find_viewport_at_impl(*popup, x - popup->popup_offset.x, y - popup->popup_offset.y, found);
            return found;
        }
        find_viewport_at_impl(root, x, y, found);
        return found;
    }

    render::Rect scrollbar_track_rect(const Element &element, float /*ui_scale*/) noexcept {
        float width = 8.0f;
        if (element.scrollbar_width) {
            width = element.scrollbar_width->value;
        }
        if (width <= 0.0f) {
            return render::Rect{};
        }
        if (is_scrollable_y(element)) {
            return render::Rect{
                    element.layout_rect.x + std::max(0.0f, element.layout_rect.w - width),
                    element.layout_rect.y,
                    width,
                    element.layout_rect.h,
            };
        }
        if (is_scrollable_x(element)) {
            return render::Rect{
                    element.layout_rect.x,
                    element.layout_rect.y + std::max(0.0f, element.layout_rect.h - width),
                    element.layout_rect.w,
                    width,
            };
        }
        return render::Rect{};
    }

    render::Rect scrollbar_thumb_rect(const Element &element, float /*ui_scale*/) noexcept {
        const render::Rect track = scrollbar_track_rect(element);
        if (track.w <= 0.0f || track.h <= 0.0f) {
            return render::Rect{};
        }
        if (is_scrollable_y(element)) {
            const float total_h = element.layout_rect.h + element.max_scroll_y;
            const float ratio = total_h > 0.0f ? std::clamp(element.layout_rect.h / total_h, 0.05f, 1.0f) : 1.0f;
            const float min_thumb_h = std::min(track.h, 20.0f);
            const float thumb_h = std::max(min_thumb_h, track.h * ratio);
            const float travel = track.h - thumb_h;
            const float offset =
                    element.max_scroll_y > 0.0f ? travel * (element.scroll_y / element.max_scroll_y) : 0.0f;
            return render::Rect{
                    track.x,
                    track.y + offset,
                    track.w,
                    thumb_h,
            };
        }
        if (is_scrollable_x(element)) {
            const float total_w = element.layout_rect.w + element.max_scroll_x;
            const float ratio = total_w > 0.0f ? std::clamp(element.layout_rect.w / total_w, 0.05f, 1.0f) : 1.0f;
            const float min_thumb_w = std::min(track.w, 20.0f);
            const float thumb_w = std::max(min_thumb_w, track.w * ratio);
            const float travel = track.w - thumb_w;
            const float offset =
                    element.max_scroll_x > 0.0f ? travel * (element.scroll_x / element.max_scroll_x) : 0.0f;
            return render::Rect{
                    track.x + offset,
                    track.y,
                    thumb_w,
                    track.h,
            };
        }
        return render::Rect{};
    }

    void find_scrollable_at_impl(Element &element, float x, float y, Element *&found) {
        if (element.kind == ElementKind::ItemTemplate || !element.visible || element.display_none) {
            return;
        }
        if (!rect_contains(element.layout_rect, x, y)) {
            return;
        }
        if (is_scrollable(element)) {
            found = &element;
        }
        float child_x = x;
        float child_y = y;
        if (element.kind == ElementKind::Viewport) {
            const glm::vec2 inverted = inverse_viewport_pointer(element, glm::vec2{x, y});
            child_x = inverted.x;
            child_y = inverted.y;
        } else if (element.scroll_x != 0.0f || element.scroll_y != 0.0f) {
            child_x = x + element.scroll_x;
            child_y = y + element.scroll_y;
        }
        for (Element *child: child_stacking_order(element.children)) {
            if (child->kind != ElementKind::Popup) {
                find_scrollable_at_impl(*child, child_x, child_y, found);
            }
        }
        for (Element *child: child_stacking_order(element.generated_items)) {
            if (child->kind != ElementKind::Popup) {
                find_scrollable_at_impl(*child, child_x, child_y, found);
            }
        }
    }

    Element *find_scrollable_at(Element &root, float x, float y) {
        Element *found = nullptr;
        // Over an open popup only that popup is searched: nothing under it scrolls or zooms.
        if (Element *popup = popup_at(root, x, y)) {
            find_scrollable_at_impl(*popup, x - popup->popup_offset.x, y - popup->popup_offset.y, found);
            return found;
        }
        find_scrollable_at_impl(root, x, y, found);
        return found;
    }

    Element *find_by_kind(Element &root, ElementKind kind) {
        return const_cast<Element *>(find_by_kind_const(root, kind));
    }

    const Element *find_by_kind(const Element &root, ElementKind kind) { return find_by_kind_const(root, kind); }

    Element *find_by_id(Element &root, std::string_view id) {
        if (root.id == id) {
            return &root;
        }
        if (root.kind == ElementKind::ItemTemplate) {
            return nullptr;
        }
        for (Element &child: root.children) {
            if (Element *found = find_by_id(child, id)) {
                return found;
            }
        }
        return nullptr;
    }

    bool scroll_item_into_view(Element &scroller, const Element &items_control, std::size_t index) {
        const std::vector<Element> &generated = items_control.generated_items;
        if (generated.empty()) {
            return false;
        }
        // Item 0 starts where the first generated element does: a leading virtualization spacer stands in
        // for the rows above the window at their full height.
        const float items_top = generated.front().layout_rect.y;
        const Element *first_row = nullptr;
        const Element *second_row = nullptr;
        std::size_t rows_shown = 0;
        float spacer_height = 0.0f;
        for (const Element &child: generated) {
            if (child.is_virtualization_spacer) {
                spacer_height += child.layout_rect.h;
                continue;
            }
            ++rows_shown;
            if (first_row == nullptr) {
                first_row = &child;
            } else if (second_row == nullptr) {
                second_row = &child;
            }
        }
        if (first_row == nullptr || first_row->layout_rect.h <= 0.0f) {
            return false;
        }
        const float row_height = first_row->layout_rect.h;
        const float stride =
                second_row != nullptr ? second_row->layout_rect.y - first_row->layout_rect.y : row_height;
        if (stride <= 0.0f) {
            return false;
        }
        const auto items = static_cast<std::size_t>(std::lround(spacer_height / stride)) + rows_shown;
        if (index >= items) {
            return false;
        }

        const float top = items_top + static_cast<float>(index) * stride;
        const float bottom = top + row_height;
        const float view_top = scroller.layout_rect.y + scroller.scroll_y;
        const float view_bottom = view_top + scroller.layout_rect.h;
        float scroll = scroller.scroll_y;
        if (top < view_top) {
            scroll = top - scroller.layout_rect.y;
        } else if (bottom > view_bottom) {
            scroll = bottom - scroller.layout_rect.y - scroller.layout_rect.h;
        }
        scroller.scroll_y = std::clamp(scroll, 0.0f, scroller.max_scroll_y);
        return true;
    }

    // Finds the (non-template) Element bind_element() most recently stamped with this exact
    // generated_owner identity — i.e. the live generated clone of a specific ItemsControl item, right
    // now, in this already-rebound tree. `owner` must come from a *freshly* re-applied bind pass (see
    // canvas.cpp update_drag()): generated_owner is only ever safe to compare, never to hold onto
    // across frames without this kind of same-frame revalidation (wind-112's reconciliation relies on
    // the same rule) — a stale owner simply won't be found if the item is gone.
    Element *find_by_generated_owner(Element &root, const void *owner) {
        if (owner == nullptr) {
            return nullptr;
        }
        if (root.generated_owner == owner) {
            return &root;
        }
        for (Element &child: root.children) {
            if (Element *found = find_by_generated_owner(child, owner)) {
                return found;
            }
        }
        for (Element &child: root.generated_items) {
            if (Element *found = find_by_generated_owner(child, owner)) {
                return found;
            }
        }
        return nullptr;
    }

} // namespace engine::ui
