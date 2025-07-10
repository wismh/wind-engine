#include <engine/ui/builder.h>

#include <cctype>
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

void report(IFatalError* fatal, std::string_view message) {
    if (fatal != nullptr) {
        fatal->report(message);
    }
}

void append_classes(std::vector<std::string>& classes, std::string_view value) {
    std::size_t pos = 0;
    while (pos < value.size()) {
        while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])) != 0) {
            ++pos;
        }
        const std::size_t begin = pos;
        while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])) == 0) {
            ++pos;
        }
        if (pos > begin) {
            classes.emplace_back(value.substr(begin, pos - begin));
        }
    }
}

}

Node::Node(ElementKind kind) {
    element_.kind = kind;
}

Node& Node::with_id(std::string_view id) {
    element_.id = std::string(id);
    return *this;
}

Node& Node::with_class(std::string_view class_name) {
    append_classes(element_.classes, class_name);
    return *this;
}

Node& Node::with_name(std::string_view name) {
    element_.name = std::string(name);
    return *this;
}

Node& Node::text(std::string_view value) {
    element_.text = std::string(value);
    return *this;
}

Node& Node::text_bind(BindingId id) {
    element_.text_binding = id;
    return *this;
}

Node& Node::content(std::string_view value) {
    element_.text = std::string(value);
    return *this;
}

Node& Node::content_bind(BindingId id) {
    element_.content_binding = id;
    return *this;
}

Node& Node::command_bind(BindingId id) {
    element_.command_binding = id;
    return *this;
}

Node& Node::paint_bind(BindingId id) {
    element_.paint_binding = id;
    return *this;
}

Node& Node::drag_bind(BindingId id) {
    element_.drag_binding = id;
    return *this;
}

Node& Node::pan_x_bind(BindingId id) {
    element_.pan_x_binding = id;
    return *this;
}

Node& Node::pan_y_bind(BindingId id) {
    element_.pan_y_binding = id;
    return *this;
}

Node& Node::zoom_bind(BindingId id) {
    element_.zoom_binding = id;
    return *this;
}

Node& Node::drag_orientation(StackDirection orientation) {
    element_.drag_orientation = orientation;
    return *this;
}

Node& Node::source(AssetId id) {
    element_.source = id;
    return *this;
}

Node& Node::source_bind(BindingId id) {
    element_.source_binding = id;
    return *this;
}

Node& Node::items_source_bind(BindingId id) {
    element_.items_source_binding = id;
    return *this;
}

Node& Node::var(std::string_view name, BindingId id) {
    if (name.empty() || !is_bound(id)) {
        return *this;
    }
    element_.custom_property_bindings.push_back(CustomPropertyBinding{std::string(name), id});
    return *this;
}

Node& Node::direction(StackDirection direction) {
    if (element_.kind == ElementKind::Stack) {
        element_.direction = direction;
    }
    return *this;
}

Node& Node::gap(float px) {
    if (element_.kind == ElementKind::Stack) {
        element_.gap = Length{px, LengthUnit::Px};
    }
    return *this;
}

Node& Node::slice(LengthInsets insets) {
    element_.slice = insets;
    return *this;
}

Node& Node::stylesheet(AssetId id) {
    stylesheet_ = id;
    return *this;
}

Node& Node::add(Node child) {
    element_.children.push_back(std::move(child.element_));
    return *this;
}

Element Node::take() && {
    return std::move(element_);
}

Node canvas() {
    return Node(ElementKind::Canvas);
}

Node stack() {
    return Node(ElementKind::Stack);
}

Node label() {
    return Node(ElementKind::Label);
}

Node button() {
    return Node(ElementKind::Button);
}

Node image() {
    return Node(ElementKind::Image);
}

Node items_control() {
    return Node(ElementKind::ItemsControl);
}

Node item_template() {
    return Node(ElementKind::ItemTemplate);
}

Node line() {
    return Node(ElementKind::Line);
}

Node component() {
    return Node(ElementKind::Component);
}

Node viewport() {
    return Node(ElementKind::Viewport);
}

Node text_input() {
    return Node(ElementKind::TextInput);
}

std::expected<UiDocument, UiError> make_document(Node root, IFatalError* fatal) {
    if (root.element_.kind != ElementKind::Canvas) {
        report(fatal, "UI document root must be a Canvas");
        return std::unexpected(UiError::InvalidMarkup);
    }
    UiDocument document;
    document.root = std::move(root.element_);
    document.stylesheet = root.stylesheet_;
    return document;
}

}
