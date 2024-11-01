#pragma once

#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <vector>

namespace engine::ui {

// Every AssetId a document's Element tree (source, recursively over children + generated_items)
// or a stylesheet's `background-image` declarations could paint. Conservative, not "currently
// visible": a rule gated behind a class/pseudo that isn't active right now is still included,
// since which rules match can change every frame (hover, disabled, ...) and re-scanning per
// canvas per frame (see run_ui_render, ecs/systems.cpp) is cheap enough not to need that precision.
[[nodiscard]] std::vector<AssetId> collect_referenced_images(const UiDocument& document, const Stylesheet* stylesheet);

// Same idea for `font-family` — Element::font_family (recursively) and stylesheet declarations.
// Never includes builtin::font_ui; that one is always ensured unconditionally (run_ui_render owns
// that call) since it's the fallback for elements with no font-family at all, and on the very
// first paint of a document Element::font_family hasn't been resolved to it yet (paint.cpp only
// writes the resolved value back after computing style).
[[nodiscard]] std::vector<AssetId> collect_referenced_fonts(const UiDocument& document, const Stylesheet* stylesheet);

}
