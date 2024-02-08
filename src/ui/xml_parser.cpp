#include <engine/ui/document.h>

#include <tinyxml2.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace engine::ui {
namespace {

std::string_view trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

void report(IFatalError* fatal, std::string_view message) {
    if (fatal != nullptr) {
        fatal->report(message);
    }
}

std::optional<ElementKind> kind_from_tag(const char* name) {
    if (name == nullptr) {
        return std::nullopt;
    }
    const std::string_view tag(name);
    if (tag == "Canvas") {
        return ElementKind::Canvas;
    }
    if (tag == "Stack") {
        return ElementKind::Stack;
    }
    if (tag == "Label") {
        return ElementKind::Label;
    }
    if (tag == "Button") {
        return ElementKind::Button;
    }
    if (tag == "Image") {
        return ElementKind::Image;
    }
    if (tag == "ItemsControl") {
        return ElementKind::ItemsControl;
    }
    if (tag == "ItemTemplate") {
        return ElementKind::ItemTemplate;
    }
    return std::nullopt;
}

// Returns nullopt if the value is a literal, empty string if the path is missing, otherwise the registered name.
std::optional<std::string> try_parse_binding(std::string_view raw) {
    const std::string_view value = trim(raw);
    constexpr std::string_view kPrefix = "{binding";
    if (value.size() < kPrefix.size() + 1 || !value.starts_with(kPrefix) || value.back() != '}') {
        return std::nullopt;
    }
    const std::string_view inner = trim(value.substr(kPrefix.size(), value.size() - kPrefix.size() - 1));
    if (inner.empty()) {
        return std::string{};
    }
    const auto eq = inner.find('=');
    if (eq != std::string_view::npos) {
        const std::string_view key = trim(inner.substr(0, eq));
        if (key == "path") {
            return std::string(trim(inner.substr(eq + 1)));
        }
    }
    return std::string(inner);
}

std::expected<void, UiError> assign_property_binding(BindingId& dest, std::string& literal, const char* attr,
        IFatalError* fatal, const ViewModel* vm, bool in_template) {
    if (attr == nullptr) {
        return {};
    }
    const auto binding = try_parse_binding(attr);
    if (!binding) {
        literal = attr;
        return {};
    }
    if (binding->empty()) {
        report(fatal, "UI binding is missing a registered name");
        return std::unexpected(UiError::MissingBinding);
    }
    dest = intern(*binding);
    if (vm != nullptr && !in_template && !vm->has_property(dest)) {
        report(fatal, "UI binding name is not registered: " + *binding);
        return std::unexpected(UiError::MissingBinding);
    }
    return {};
}

std::expected<void, UiError> assign_command_binding(Element& element, const char* attr, IFatalError* fatal,
        const ViewModel* vm, bool in_template) {
    if (attr == nullptr) {
        return {};
    }
    const auto binding = try_parse_binding(attr);
    if (!binding) {
        report(fatal, "UI command must be a {binding} path");
        return std::unexpected(UiError::MissingBinding);
    }
    if (binding->empty()) {
        report(fatal, "UI binding is missing a registered name");
        return std::unexpected(UiError::MissingBinding);
    }
    element.command_binding = intern(*binding);
    if (vm != nullptr && !in_template && !vm->has_command(element.command_binding)) {
        report(fatal, "UI binding name is not registered: " + *binding);
        return std::unexpected(UiError::MissingBinding);
    }
    return {};
}

std::expected<void, UiError> parse_source(Element& element, const char* attr, IFatalError* fatal, const ViewModel* vm,
        bool in_template) {
    if (attr == nullptr) {
        return {};
    }
    const auto binding = try_parse_binding(attr);
    if (binding) {
        if (binding->empty()) {
            report(fatal, "UI binding is missing a registered name");
            return std::unexpected(UiError::MissingBinding);
        }
        element.source_binding = intern(*binding);
        if (vm != nullptr && !in_template && !vm->has_property(element.source_binding)) {
            report(fatal, "UI binding name is not registered: " + *binding);
            return std::unexpected(UiError::MissingBinding);
        }
        return {};
    }
    const auto id = AssetId::parse(attr);
    if (!id) {
        report(fatal, "UI Image source must be a 32-hex AssetId or {binding}, not a filename");
        return std::unexpected(UiError::ForbiddenContent);
    }
    element.source = *id;
    return {};
}

std::expected<Element, UiError> parse_element(const tinyxml2::XMLElement* xml, IFatalError* fatal, const ViewModel* vm,
        bool in_template) {
    const auto kind = kind_from_tag(xml->Name());
    if (!kind) {
        std::string message = "unknown UI element: ";
        message += xml->Name() != nullptr ? xml->Name() : "(null)";
        report(fatal, message);
        return std::unexpected(UiError::UnknownElement);
    }

    Element element;
    element.kind = *kind;

    if (const char* id = xml->Attribute("id")) {
        element.id = id;
    }
    if (const char* cls = xml->Attribute("class")) {
        element.class_name = cls;
    }
    if (const char* name_attr = xml->Attribute("name")) {
        element.name = name_attr;
    }

    if (element.kind == ElementKind::Stack) {
        if (const char* direction = xml->Attribute("direction")) {
            const std::string_view dir = trim(direction);
            if (dir == "horizontal" || dir == "row") {
                element.direction = StackDirection::Horizontal;
            } else {
                element.direction = StackDirection::Vertical;
            }
        }
        if (const char* gap = xml->Attribute("gap")) {
            element.gap = std::strtof(gap, nullptr);
        }
    }

    if (auto result = assign_property_binding(element.text_binding, element.text, xml->Attribute("text"), fatal, vm,
                in_template);
            !result) {
        return std::unexpected(result.error());
    }
    if (auto result = assign_property_binding(element.content_binding, element.text, xml->Attribute("content"), fatal, vm,
                in_template);
            !result) {
        return std::unexpected(result.error());
    }
    if (auto result = assign_command_binding(element, xml->Attribute("command"), fatal, vm, in_template); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = parse_source(element, xml->Attribute("source"), fatal, vm, in_template); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = assign_property_binding(element.items_source_binding, element.text, xml->Attribute("items_source"),
                fatal, vm, in_template);
            !result) {
        return std::unexpected(result.error());
    }

    const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
    for (const tinyxml2::XMLElement* child = xml->FirstChildElement(); child != nullptr;
            child = child->NextSiblingElement()) {
        auto parsed = parse_element(child, fatal, vm, nested_template);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        element.children.push_back(std::move(*parsed));
    }
    return element;
}

}

std::expected<UiDocument, UiError> parse_xml(std::string_view xml, IFatalError* fatal, const ViewModel* data_context) {
    tinyxml2::XMLDocument doc;
    const tinyxml2::XMLError parsed = doc.Parse(xml.data(), xml.size());
    if (parsed != tinyxml2::XML_SUCCESS || doc.RootElement() == nullptr) {
        report(fatal, "invalid UI XML");
        return std::unexpected(UiError::InvalidMarkup);
    }

    auto root = parse_element(doc.RootElement(), fatal, data_context, false);
    if (!root) {
        return std::unexpected(root.error());
    }

    UiDocument document;
    document.root = std::move(*root);
    if (const char* stylesheet = doc.RootElement()->Attribute("stylesheet")) {
        if (const auto id = AssetId::parse(stylesheet)) {
            document.stylesheet = *id;
        } else {
            report(fatal, "UI Canvas stylesheet must be a 32-hex AssetId");
            return std::unexpected(UiError::ForbiddenContent);
        }
    }
    return document;
}

}
