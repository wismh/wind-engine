#pragma once

// docs/tech/modules/UI.md#pictures

#include "ui/dirty.h"
#include "ui/painter.h"
#include "ui/picture.h"

#include <engine/ui/document.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace engine::ui {

    // Pictures are used while a document paints through `paint_document` (or its popup layer). Outside it, and
    // while the inspector draws over the document, an element is painted straight to the painter.
    struct PictureContext {
        UiDocument *document = nullptr;
        // The part being recorded: what it draws that changes without a mark makes it volatile.
        PicturePart *recording = nullptr;
        ElementId recording_owner{};
        // The debug check: every boundary records afresh and is compared with what it holds.
        bool checking = false;
        std::uint64_t check_failures = 0;
    };

    // The thread's picture state.
    PictureContext &picture_context();

    // Not painted now: nothing under it changes what a picture above it shows. A closed popup is painted by nobody.
    [[nodiscard]] bool element_hidden(const Element &e) noexcept;

    // Elements that keep a picture of their own: the root, anything that scrolls, a camera, a popup, and what the
    // stylesheet marks with `repaint-boundary`. Decided from the element alone, so a pass that looks for marks can tell
    // where one picture ends and the next begins without a style.
    [[nodiscard]] bool is_boundary_element(const Element &e) noexcept;

    [[nodiscard]] bool contains_rect(const render::Rect &outer, const render::Rect &inner) noexcept;

    inline constexpr std::uint8_t kPaintMarks = kDirtyPaint | kDirtyFrame;

    [[nodiscard]] const std::vector<Element> &paint_children(const Element &e);

    // A plain element of a boundary's region (not a boundary itself, not hidden) holds a paint mark: the boundary's
    // picture shows something that changed. Looks only along the marks.
    [[nodiscard]] bool plain_dirty(const Element &e);
    [[nodiscard]] bool region_dirty(const std::vector<Element> &children);

    // Clears the marks of a boundary's region once its pictures are up to date. A nested boundary keeps its own marks
    // until it is brought up to date, a hidden element keeps them until it is shown (showing it marks it again).
    void clear_region_marks(Element &boundary, std::vector<Element> &children);

    // What a recording notes: the picture being recorded will not be reusable (a blinking caret, an `IPaint` that
    // cannot say when it changed), or depends on an `IPaint`'s version.
    void note_volatile();
    void note_paint(const Element &carrier);

    // Runs `body` against a recording painter into `part`, as the picture of `owner`, and notes what the picture
    // depends on beyond its key. In the debug check the fresh recording is compared with the picture the part
    // holds and dropped.
    template<typename Body>
    void record_part(PictureContext &context, Element &owner, PicturePart &part, IUiPainter &painter, Body &&body) {
        PicturePart fresh;
        PicturePart *target = context.checking ? &fresh : &part;
        if (!target->picture) {
            target->picture = std::make_shared<Picture>();
        }
        target->picture->clear();
        target->volatile_part = false;
        target->watches.clear();
        PicturePart *outer = context.recording;
        const ElementId outer_owner = context.recording_owner;
        context.recording = target;
        context.recording_owner = owner.handle;
        {
            RecordingPainter recording(painter, *target->picture);
            body(recording);
            if (recording.saw_empty_measure()) {
                target->volatile_part = true;
            }
        }
        context.recording = outer;
        context.recording_owner = outer_owner;
        target->valid = true;
        UiDocument &document = *context.document;
        if (context.checking) {
            ++document.counters.picture_checks;
            if (part.valid && !part.volatile_part && !target->volatile_part && part.picture != nullptr &&
                !(*part.picture == *target->picture)) {
                ++document.counters.picture_check_failures;
                ++context.check_failures;
                std::string classes;
                for (const std::string &name: owner.classes) {
                    classes += "." + name;
                }
                std::fprintf(stderr,
                             "UI picture check: a replayed picture differs from a fresh recording: %s %s "
                             "(%zu commands held, %zu recorded)\n",
                             owner.kind == ElementKind::Viewport ? "Viewport" : "element", classes.c_str(),
                             part.picture->commands.size(), target->picture->commands.size());
                std::fflush(stderr);
                if (document.abort_on_layout_mismatch) {
                    std::abort();
                }
            }
            return;
        }
        ++document.counters.pictures_recorded;
        document.counters.picture_commands += part.picture->commands.size();
        if (part.volatile_part) {
            if (std::ranges::find(document.picture_volatile, owner.handle) == document.picture_volatile.end()) {
                document.picture_volatile.push_back(owner.handle);
            }
        }
        for (const PictureWatch &watch: part.watches) {
            auto it = std::ranges::find_if(document.picture_watches, [&](const UiDocument::PaintWatch &w) {
                return w.element == owner.handle && w.carrier == watch.carrier && w.paint == watch.paint;
            });
            if (it == document.picture_watches.end()) {
                document.picture_watches.push_back({owner.handle, watch.carrier, watch.paint, watch.version});
            } else {
                it->seen = watch.version;
            }
        }
    }

    // Makes `document` the one whose elements keep pictures while it lives, and marks what has to be recorded again
    // without anything having changed in the tree: the pictures that can never be replayed and the ones whose
    // `IPaint` moved to another version.
    class PictureScope {
    public:
        explicit PictureScope(UiDocument &document, bool mark_unstable = true) : context_(picture_context()) {
            previous_ = context_.document;
            context_.document = &document;
            if (mark_unstable) {
                mark_unstable_pictures(document);
            }
        }
        ~PictureScope() { context_.document = previous_; }
        PictureScope(const PictureScope &) = delete;
        PictureScope &operator=(const PictureScope &) = delete;

    private:
        static void mark_unstable_pictures(UiDocument &document) {
            std::erase_if(document.picture_volatile, [&](ElementId id) {
                const auto found = document.find(id);
                if (!found) {
                    return true;
                }
                Element &element = **found;
                if (!element.picture.chrome.volatile_part && !element.picture.content.volatile_part) {
                    return true;
                }
                mark_dirty(document, element, kDirtyPaint);
                return false;
            });
            std::erase_if(document.picture_watches, [&](UiDocument::PaintWatch &watch) {
                const auto found = document.find(watch.element);
                const auto carrier = document.find(watch.carrier);
                if (!found || !carrier) {
                    return true;
                }
                Element &element = **found;
                const auto holds = [&](const PicturePart &part) {
                    return std::ranges::any_of(part.watches, [&](const PictureWatch &w) {
                        return w.carrier == watch.carrier && w.paint == watch.paint;
                    });
                };
                if (!holds(element.picture.chrome) && !holds(element.picture.content)) {
                    // The boundary records something else now.
                    return true;
                }
                // The carrier's own pointer is the live one; the copy kept at record time may be dangling.
                if ((*carrier)->paint != watch.paint) {
                    mark_dirty(document, element, kDirtyPaint);
                    return true;
                }
                const std::uint64_t version = watch.paint->version();
                if (version != watch.seen) {
                    watch.seen = version;
                    mark_dirty(document, element, kDirtyPaint);
                }
                return false;
            });
        }

        PictureContext &context_;
        UiDocument *previous_ = nullptr;
    };


} // namespace engine::ui
