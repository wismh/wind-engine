#include "svg_parser.h"

#include "svg_internal.h"

#include <algorithm>

namespace engine::ui::svg_detail {
namespace {

constexpr int kMaxDepth = 64;

glm::vec4 painted(PaintColor color, float alpha) {
    return {color.color.r, color.color.g, color.color.b, color.color.a * alpha};
}

void emit(SvgPicture& picture, std::vector<PathSegment> segments, const Style& style, Aff2 xform) {
    if (segments.empty()) {
        return;
    }
    bake(segments, xform);
    const float width = style.stroke_width * average_scale(xform);
    if (style.fill.kind != ColorKind::None) {
        SvgShape shape;
        shape.segments = segments;
        shape.paint = SvgShape::Paint::Fill;
        shape.current_color = style.fill.kind == ColorKind::Current;
        shape.color = painted(style.fill, style.opacity * style.fill_opacity);
        picture.shapes.push_back(std::move(shape));
    }
    if (style.stroke.kind != ColorKind::None && width > 0.0f) {
        SvgShape shape;
        shape.segments = std::move(segments);
        shape.paint = SvgShape::Paint::Stroke;
        shape.width = width;
        shape.current_color = style.stroke.kind == ColorKind::Current;
        shape.color = painted(style.stroke, style.opacity * style.stroke_opacity);
        picture.shapes.push_back(std::move(shape));
    }
}

bool emit_shape(SvgPicture& picture, const tinyxml2::XMLElement* element, const Style& style, Aff2 xform) {
    const std::string_view tag = element->Name();
    std::vector<PathSegment> segments;
    bool ok = true;
    if (tag == "path") {
        const char* data = element->Attribute("d");
        if (data == nullptr || !parse_path_data(data, segments)) {
            return false;
        }
    } else if (tag == "rect") {
        const float x = attr_length(element, "x", 0.0f, ok);
        const float y = attr_length(element, "y", 0.0f, ok);
        const float w = attr_length(element, "width", 0.0f, ok);
        const float h = attr_length(element, "height", 0.0f, ok);
        float rx = attr_length(element, "rx", -1.0f, ok);
        float ry = attr_length(element, "ry", -1.0f, ok);
        if (!ok || !(w > 0.0f) || !(h > 0.0f)) {
            return ok;
        }
        if (rx < 0.0f && ry < 0.0f) {
            rx = 0.0f;
            ry = 0.0f;
        } else if (rx < 0.0f) {
            rx = ry;
        } else if (ry < 0.0f) {
            ry = rx;
        }
        add_rounded_rect(segments, x, y, w, h, rx, ry);
    } else if (tag == "circle" || tag == "ellipse") {
        const float cx = attr_length(element, "cx", 0.0f, ok);
        const float cy = attr_length(element, "cy", 0.0f, ok);
        const float rx = tag == "circle" ? attr_length(element, "r", 0.0f, ok) : attr_length(element, "rx", 0.0f, ok);
        const float ry = tag == "circle" ? rx : attr_length(element, "ry", 0.0f, ok);
        if (!ok || !(rx > 0.0f) || !(ry > 0.0f)) {
            return ok;
        }
        add_ellipse(segments, cx, cy, rx, ry);
    } else if (tag == "line") {
        const float x1 = attr_length(element, "x1", 0.0f, ok);
        const float y1 = attr_length(element, "y1", 0.0f, ok);
        const float x2 = attr_length(element, "x2", 0.0f, ok);
        const float y2 = attr_length(element, "y2", 0.0f, ok);
        if (!ok) {
            return false;
        }
        add_segment(segments, PathSegment::Kind::Move, {x1, y1});
        add_segment(segments, PathSegment::Kind::Line, {x2, y2});
    } else if (tag == "polyline" || tag == "polygon") {
        const char* value = element->Attribute("points");
        std::vector<glm::vec2> points;
        if (value == nullptr || !parse_points(value, points)) {
            return false;
        }
        add_segment(segments, PathSegment::Kind::Move, points.front());
        for (std::size_t i = 1; i < points.size(); ++i) {
            add_segment(segments, PathSegment::Kind::Line, points[i]);
        }
        if (tag == "polygon") {
            add_segment(segments, PathSegment::Kind::Close, {});
        }
    } else {
        return false;
    }
    emit(picture, std::move(segments), style, xform);
    return true;
}

bool forbidden(std::string_view tag) {
    return tag == "text" || tag == "tspan" || tag == "textPath" || tag == "image" || tag == "use" || tag == "style" ||
           tag == "linearGradient" || tag == "radialGradient" || tag == "filter" || tag == "clipPath" ||
           tag == "mask" || tag == "pattern" || tag == "symbol" || tag == "marker" || tag == "script" ||
           tag == "foreignObject" || tag == "animate" || tag == "animateTransform" || tag == "set" || tag == "switch";
}

bool walk(SvgPicture& picture, const tinyxml2::XMLElement* element, Style style, Aff2 xform, bool draw, int depth) {
    if (depth > kMaxDepth || element->Name() == nullptr) {
        return false;
    }
    const std::string_view tag = element->Name();
    if (tag == "title" || tag == "desc" || tag == "metadata") {
        return true;
    }
    if (forbidden(tag)) {
        return false;
    }
    if (tag == "defs") {
        for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr;
             child = child->NextSiblingElement()) {
            if (!walk(picture, child, style, xform, false, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (tag == "g") {
        if (!apply_presentation(style, element) || !element_transform(element, xform, xform)) {
            return false;
        }
        if (style.skip) {
            return true;
        }
        for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr;
             child = child->NextSiblingElement()) {
            if (!walk(picture, child, style, xform, draw, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (tag == "path" || tag == "rect" || tag == "circle" || tag == "ellipse" || tag == "line" || tag == "polyline" ||
        tag == "polygon") {
        if (!apply_presentation(style, element) || !element_transform(element, xform, xform)) {
            return false;
        }
        if (!draw || style.skip) {
            return true;
        }
        return emit_shape(picture, element, style, xform);
    }
    return false;
}

std::optional<SvgPicture> parse_root(const tinyxml2::XMLElement* root) {
    if (root == nullptr || std::string_view{root->Name()} != "svg") {
        return std::nullopt;
    }
    const char* box = root->Attribute("viewBox");
    if (box == nullptr) {
        return std::nullopt;
    }
    Numbers numbers{box};
    const float min_x = numbers.number();
    const float min_y = numbers.number();
    const float width = numbers.number();
    const float height = numbers.number();
    numbers.skip();
    if (!numbers.ok || numbers.i != numbers.text.size() || !(width > 0.0f) || !(height > 0.0f)) {
        return std::nullopt;
    }
    Style style;
    Aff2 xform;
    if (!apply_presentation(style, root) || !element_transform(root, Aff2{}, xform)) {
        return std::nullopt;
    }
    SvgPicture picture;
    picture.min_x = min_x;
    picture.min_y = min_y;
    picture.width = width;
    picture.height = height;
    if (style.skip) {
        return picture;
    }
    for (const tinyxml2::XMLElement* child = root->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (!walk(picture, child, style, xform, true, 1)) {
            return std::nullopt;
        }
    }
    return picture;
}

}
}

namespace engine::ui {

std::optional<SvgPicture> parse_svg(std::string_view xml) {
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return std::nullopt;
    }
    return svg_detail::parse_root(document.RootElement());
}

}
