#include "ui/layout_check.h"

#if defined(ENGINE_UI_LAYOUT_CHECK)

#include "ui/style_anim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace engine::ui {

    namespace {

        // The scratch copy starts with no memory of an earlier layout: caches empty, nothing marked.
        void drop_caches(Element &element) {
            element.style_cache_layout_ = StyleCacheEntry{};
            element.style_cache_paint_ = StyleCacheEntry{};
            element.text_measure_cache_valid = false;
            element.text_wrap_cache_valid = false;
            element.layout_used_cache_valid = false;
            element.layout_inputs_changed = false;
            element.layout_descendant_inputs_changed = false;
            element.dirty_self = 0;
            element.dirty_below = 0;
            for (Element &child: element.children) {
                drop_caches(child);
            }
            for (Element &item: element.generated_items) {
                drop_caches(item);
            }
        }

        bool close(float a, float b) { return std::fabs(a - b) <= 0.01f; }

        std::string describe(const Element &element, const std::string &path) {
            std::string out = path;
            out += std::format(" ({}", static_cast<int>(element.kind));
            if (!element.id.empty()) {
                out += " #" + element.id;
            }
            for (const std::string &name: element.classes) {
                out += " ." + name;
            }
            out += ")";
            return out;
        }

        // The first element whose rect differs, with both rects; empty when the trees agree.
        std::string first_difference(const Element &live, const Element &fresh, const std::string &path) {
            if (live.kind == ElementKind::ItemTemplate) {
                return {};
            }
            if (!close(live.layout_rect.x, fresh.layout_rect.x) || !close(live.layout_rect.y, fresh.layout_rect.y) ||
                !close(live.layout_rect.w, fresh.layout_rect.w) || !close(live.layout_rect.h, fresh.layout_rect.h)) {
                return std::format("{}: live {:.2f},{:.2f} {:.2f}x{:.2f}, from scratch {:.2f},{:.2f} {:.2f}x{:.2f}",
                                   describe(live, path), live.layout_rect.x, live.layout_rect.y, live.layout_rect.w,
                                   live.layout_rect.h, fresh.layout_rect.x, fresh.layout_rect.y, fresh.layout_rect.w,
                                   fresh.layout_rect.h);
            }
            if (live.children.size() != fresh.children.size() ||
                live.generated_items.size() != fresh.generated_items.size()) {
                return describe(live, path) + ": the copy has a different number of children";
            }
            for (std::size_t i = 0; i < live.children.size(); ++i) {
                std::string found =
                        first_difference(live.children[i], fresh.children[i], path + "/" + std::to_string(i));
                if (!found.empty()) {
                    return found;
                }
            }
            for (std::size_t i = 0; i < live.generated_items.size(); ++i) {
                std::string found = first_difference(live.generated_items[i], fresh.generated_items[i],
                                                     path + "/g" + std::to_string(i));
                if (!found.empty()) {
                    return found;
                }
            }
            return {};
        }

    } // namespace

    void verify_layout(UiDocument &document, const Stylesheet *sheet, IUiPainter *painter,
                       const render::Rect &canvas_rect, float media_width, float media_height) {
        if (!document.verify_layout) {
            return;
        }
        UiDocument scratch = document;
        drop_caches(scratch.root);
        apply_layout_style(scratch.root, sheet, media_width, media_height);
        commit_motion_after_layout(scratch.root);
        layout(scratch, canvas_rect, painter, false);
        ++document.counters.layout_checks;
        const std::string difference = first_difference(document.root, scratch.root, "root");
        if (difference.empty()) {
            return;
        }
        ++document.counters.layout_check_failures;
        std::fprintf(stderr, "UI layout check: a skipped layout left a stale rect: %s\n", difference.c_str());
        std::fflush(stderr);
        if (document.abort_on_layout_mismatch) {
            std::abort();
        }
    }

} // namespace engine::ui

#endif
