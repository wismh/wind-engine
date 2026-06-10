#pragma once

#include <engine/resources/assets_db.h>
#include <engine/ui/document.h>

namespace engine::ui {

// Resolves every `Svg` element's `source` onto `Element::svg` before the canvas is drawn.
void attach_svg_pictures(UiDocument &document, AssetsDb &assets);

}
