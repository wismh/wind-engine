#include "svg_attach.h"

#include <engine/ui/svg.h>

namespace engine::ui {
namespace {

void attach(Element &element, AssetsDb &assets) {
    if (element.kind == ElementKind::Svg) {
        if (element.source) {
            element.svg = assets.get<SvgPicture>(*element.source);
        } else {
            element.svg.reset();
        }
    }
    for (Element &child: element.children) {
        attach(child, assets);
    }
    for (Element &child: element.generated_items) {
        attach(child, assets);
    }
}

}

void attach_svg_pictures(UiDocument &document, AssetsDb &assets) {
    attach(document.root, assets);
}

}
