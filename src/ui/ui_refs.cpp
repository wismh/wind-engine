#include "ui_refs.h"

#include <engine/builtin_ids.h>

#include <set>

namespace engine::ui {
namespace {

void collect_element_images(const Element& element, std::set<AssetId>& out) {
    if (element.source) {
        out.insert(*element.source);
    }
    for (const Element& child : element.children) {
        collect_element_images(child, out);
    }
    for (const Element& child : element.generated_items) {
        collect_element_images(child, out);
    }
}

void collect_element_fonts(const Element& element, std::set<AssetId>& out) {
    if (element.font_family != AssetId{} && element.font_family != builtin::font_ui) {
        out.insert(element.font_family);
    }
    for (const Element& child : element.children) {
        collect_element_fonts(child, out);
    }
    for (const Element& child : element.generated_items) {
        collect_element_fonts(child, out);
    }
}

}

std::vector<AssetId> collect_referenced_images(const UiDocument& document, const Stylesheet* stylesheet) {
    std::set<AssetId> ids;
    collect_element_images(document.root, ids);
    if (stylesheet != nullptr) {
        for (const CssRule& rule : stylesheet->rules) {
            for (const CssDeclaration& decl : rule.declarations) {
                if (decl.property != "background-image" || decl.value == "none") {
                    continue;
                }
                if (const auto id = AssetId::parse(decl.value)) {
                    ids.insert(*id);
                }
            }
        }
    }
    return std::vector<AssetId>(ids.begin(), ids.end());
}

std::vector<AssetId> collect_referenced_fonts(const UiDocument& document, const Stylesheet* stylesheet) {
    std::set<AssetId> ids;
    collect_element_fonts(document.root, ids);
    if (stylesheet != nullptr) {
        for (const CssRule& rule : stylesheet->rules) {
            for (const CssDeclaration& decl : rule.declarations) {
                if (decl.property != "font-family") {
                    continue;
                }
                if (const auto id = AssetId::parse(decl.value); id && *id != builtin::font_ui) {
                    ids.insert(*id);
                }
            }
        }
    }
    return std::vector<AssetId>(ids.begin(), ids.end());
}

}
