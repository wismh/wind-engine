#include "scene_points.h"

#include <algorithm>

namespace bench {
namespace {

std::optional<engine::render::Rect> overlap(const engine::render::Rect& a, const engine::render::Rect& b) {
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.w, b.x + b.w);
    const float bottom = std::min(a.y + a.h, b.y + b.h);
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }
    return engine::render::Rect{left, top, right - left, bottom - top};
}

std::optional<glm::vec2> visible_middle(engine::ui::Element& root, const engine::ui::Element& element,
        const engine::render::Rect& bounds) {
    const engine::ui::LayoutBoxes boxes = engine::ui::layout_boxes(root, element);
    const std::optional<engine::render::Rect> shown = overlap(boxes.border, bounds);
    if (!shown) {
        return std::nullopt;
    }
    return glm::vec2{shown->x + shown->w * 0.5f, shown->y + shown->h * 0.5f};
}

bool has_class(const engine::ui::Element& element, std::string_view name) {
    return std::ranges::find(element.classes, name) != element.classes.end();
}

void collect_hot(engine::ui::Element& element, std::vector<engine::ui::Element*>& out) {
    if (element.kind == engine::ui::ElementKind::ItemTemplate || element.display_none) {
        return;
    }
    if (has_class(element, kHoverClass)) {
        out.push_back(&element);
    }
    for (engine::ui::Element& child : element.children) {
        collect_hot(child, out);
    }
    for (engine::ui::Element& child : element.generated_items) {
        collect_hot(child, out);
    }
}

}

std::optional<glm::vec2> element_point(engine::ui::Element& root, std::string_view id,
        const engine::render::Rect& bounds) {
    const engine::ui::Element* element = engine::ui::find_by_id(root, id);
    if (element == nullptr) {
        return std::nullopt;
    }
    return visible_middle(root, *element, bounds);
}

std::vector<glm::vec2> hover_points(engine::ui::Element& root, const engine::render::Rect& bounds) {
    std::vector<engine::ui::Element*> hot;
    collect_hot(root, hot);
    std::vector<glm::vec2> points;
    for (const engine::ui::Element* element : hot) {
        const std::optional<glm::vec2> middle = visible_middle(root, *element, bounds);
        if (middle && engine::ui::hit_test(root, middle->x, middle->y) == element) {
            points.push_back(*middle);
        }
    }
    return points;
}

}
