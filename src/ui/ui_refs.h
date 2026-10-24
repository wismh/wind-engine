#pragma once

#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <vector>

namespace engine::ui {

// Every texture AssetId a document could paint: an Image `source` (recursively over children and
// generated items) or a stylesheet `background-image`. An Svg `source` is not one of these.
// Conservative, not "currently visible": a rule gated behind a class or pseudo that is not active is still included,
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
