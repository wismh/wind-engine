#pragma once

// docs/tech/modules/UI.md#debug-layout-check

#include "ui/painter.h"

namespace engine::ui {

#if defined(ENGINE_UI_LAYOUT_CHECK)
    inline constexpr bool kUiLayoutCheckBuilt = true;

    // Lays a copy of `document` out from scratch (every cache and mark dropped, a full restyle, a full layout) and
    // compares every element's rect with the live tree's. A difference means a change reached the tree without marking
    // what it feeds, so layout skipped work it owed: the check prints the first mismatch with the element's path and
    // counts it (`UiWorkCounters::layout_check_failures`). Compiled only in Debug and in builds configured with
    // `ENGINE_UI_LAYOUT_CHECK`; never in a Release build.
    void verify_layout(UiDocument &document, const Stylesheet *sheet, IUiPainter *painter,
                       const render::Rect &canvas_rect, float media_width, float media_height);
#else
    inline constexpr bool kUiLayoutCheckBuilt = false;
    inline void verify_layout(UiDocument &, const Stylesheet *, IUiPainter *, const render::Rect &, float, float) {}
#endif

} // namespace engine::ui
