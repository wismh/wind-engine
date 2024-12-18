#include <engine/ui/document.h>

#include "ui/bind_scan.h"
#include "ui/css_length.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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

// Attributes named `var-<name>="{binding path}"` become CustomPropertyBinding{name, id} — resolved
// every frame in bind_element and substituted for `var(--<name>)` in CSS declarations (paint.cpp).
// Always a {binding}, never a literal: a static override belongs in the stylesheet as `--name: ...;`.
std::expected<void, UiError> parse_custom_properties(
        Element& element, const tinyxml2::XMLElement* xml, IFatalError* fatal, const ViewModel* vm, bool in_template) {
    constexpr std::string_view kPrefix = "var-";
    for (const tinyxml2::XMLAttribute* attr = xml->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
        const std::string_view attr_name = attr->Name() != nullptr ? attr->Name() : "";
        if (!attr_name.starts_with(kPrefix) || attr_name.size() == kPrefix.size()) {
            continue;
        }
        const std::string_view name = attr_name.substr(kPrefix.size());
        const std::string_view value = attr->Value() != nullptr ? attr->Value() : "";
        const auto binding = try_parse_binding(value);
        if (!binding) {
            report(fatal, "UI custom property must be a {binding} path: " + std::string(attr_name));
            return std::unexpected(UiError::ForbiddenContent);
        }
        if (binding->empty()) {
            report(fatal, "UI binding is missing a registered name");
            return std::unexpected(UiError::MissingBinding);
        }
        const BindingId id = intern(*binding);
        if (vm != nullptr && !in_template && !vm->has_property(id)) {
            report(fatal, "UI binding name is not registered: " + *binding);
            return std::unexpected(UiError::MissingBinding);
        }
        element.custom_property_bindings.push_back(CustomPropertyBinding{std::string(name), id});
    }
    return {};
}

std::expected<Element, UiError> parse_element(const tinyxml2::XMLElement* xml, IFatalError* fatal, const ViewModel* vm,
        bool in_template, const UiIncludeResolver& resolve_include, std::vector<std::string>& include_stack) {
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
            element.gap = Length{std::strtof(gap, nullptr), LengthUnit::Px};
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
    if (const char* slice_attr = xml->Attribute("slice")) {
        const auto insets = css_length::parse_insets(slice_attr);
        if (!insets) {
            report(fatal, "UI element slice must be 1 to 4 lengths: " + std::string(slice_attr));
            return std::unexpected(UiError::InvalidMarkup);
        }
        element.slice = *insets;
    }
    if (auto result = assign_property_binding(element.items_source_binding, element.text, xml->Attribute("items_source"),
                fatal, vm, in_template);
            !result) {
        return std::unexpected(result.error());
    }
    if (auto result = parse_custom_properties(element, xml, fatal, vm, in_template); !result) {
        return std::unexpected(result.error());
    }

    if (element.kind == ElementKind::ItemTemplate) {
        if (const char* src = xml->Attribute("src")) {
            if (xml->FirstChildElement() != nullptr) {
                report(fatal, std::string("ItemTemplate with src must not declare inline children: ") + src);
                return std::unexpected(UiError::InvalidMarkup);
            }
            const std::string key(src);
            if (!resolve_include) {
                report(fatal, "ItemTemplate src is not supported in this context: " + key);
                return std::unexpected(UiError::Io);
            }
            if (std::find(include_stack.begin(), include_stack.end(), key) != include_stack.end()) {
                report(fatal, "ItemTemplate src forms an include cycle: " + key);
                return std::unexpected(UiError::CyclicInclude);
            }
            const auto text = resolve_include(key);
            if (!text) {
                report(fatal, "ItemTemplate src could not be read: " + key);
                return std::unexpected(UiError::Io);
            }
            tinyxml2::XMLDocument included_doc;
            if (included_doc.Parse(text->data(), text->size()) != tinyxml2::XML_SUCCESS ||
                    included_doc.RootElement() == nullptr) {
                report(fatal, "ItemTemplate src is not valid XML: " + key);
                return std::unexpected(UiError::InvalidMarkup);
            }
            include_stack.push_back(key);
            auto included = parse_element(
                    included_doc.RootElement(), fatal, vm, /*in_template=*/true, resolve_include, include_stack);
            include_stack.pop_back();
            if (!included) {
                return std::unexpected(included.error());
            }
            element.children.push_back(std::move(*included));
            return element;
        }
    }

    const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
    for (const tinyxml2::XMLElement* child = xml->FirstChildElement(); child != nullptr;
            child = child->NextSiblingElement()) {
        auto parsed = parse_element(child, fatal, vm, nested_template, resolve_include, include_stack);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        element.children.push_back(std::move(*parsed));
    }
    return element;
}

void add_bind_member(BindBinder& binder, std::string path, bool is_command) {
    for (const BindMember& existing : binder.members) {
        if (existing.path == path) {
            return;
        }
    }
    binder.members.push_back(BindMember{std::move(path), is_command});
}

void add_bind_attr(BindBinder& binder, const char* attr, bool is_command) {
    if (attr == nullptr) {
        return;
    }
    const auto binding = try_parse_binding(attr);
    if (!binding || binding->empty()) {
        return;
    }
    add_bind_member(binder, *binding, is_command);
}

void add_bind_custom_properties(BindBinder& binder, const tinyxml2::XMLElement* xml) {
    constexpr std::string_view kPrefix = "var-";
    for (const tinyxml2::XMLAttribute* attr = xml->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
        const std::string_view attr_name = attr->Name() != nullptr ? attr->Name() : "";
        if (!attr_name.starts_with(kPrefix) || attr_name.size() == kPrefix.size()) {
            continue;
        }
        add_bind_attr(binder, attr->Value(), false);
    }
}

BindBinder& nested_binder(BindBinder& binder, const std::string& items_path) {
    for (auto& entry : binder.nested) {
        if (entry.first == items_path) {
            return entry.second;
        }
    }
    binder.nested.emplace_back(items_path, BindBinder{});
    return binder.nested.back().second;
}

void collect_bind_element(const tinyxml2::XMLElement* xml, BindBinder& binder, const UiIncludeResolver& resolve_include,
        std::vector<std::string>& include_stack);

// Resolves and scans an `<ItemTemplate src="...">` reference the same way parse_element does.
// scan_bind_tree already re-parses the whole tree via parse_xml first, which validates every
// include (missing file, cycle, invalid XML) — so a failure here is silently skipped rather than
// reported a second time.
void collect_bind_include(const char* src, BindBinder& binder, const UiIncludeResolver& resolve_include,
        std::vector<std::string>& include_stack) {
    if (!resolve_include) {
        return;
    }
    const std::string key(src);
    if (std::find(include_stack.begin(), include_stack.end(), key) != include_stack.end()) {
        return;
    }
    const auto text = resolve_include(key);
    if (!text) {
        return;
    }
    tinyxml2::XMLDocument doc;
    if (doc.Parse(text->data(), text->size()) != tinyxml2::XML_SUCCESS || doc.RootElement() == nullptr) {
        return;
    }
    include_stack.push_back(key);
    collect_bind_element(doc.RootElement(), binder, resolve_include, include_stack);
    include_stack.pop_back();
}

void collect_bind_element(const tinyxml2::XMLElement* xml, BindBinder& binder, const UiIncludeResolver& resolve_include,
        std::vector<std::string>& include_stack) {
    add_bind_attr(binder, xml->Attribute("text"), false);
    add_bind_attr(binder, xml->Attribute("content"), false);
    add_bind_attr(binder, xml->Attribute("command"), true);
    add_bind_attr(binder, xml->Attribute("source"), false);
    add_bind_attr(binder, xml->Attribute("items_source"), false);
    add_bind_custom_properties(binder, xml);

    std::string items_path;
    if (kind_from_tag(xml->Name()) == ElementKind::ItemsControl) {
        if (const char* attr = xml->Attribute("items_source")) {
            const auto binding = try_parse_binding(attr);
            if (binding) {
                items_path = *binding;
            }
        }
    }

    for (const tinyxml2::XMLElement* child = xml->FirstChildElement(); child != nullptr;
            child = child->NextSiblingElement()) {
        const bool is_item_template = kind_from_tag(child->Name()) == ElementKind::ItemTemplate;
        BindBinder& target = is_item_template ? nested_binder(binder, items_path) : binder;
        if (is_item_template) {
            if (const char* src = child->Attribute("src")) {
                collect_bind_include(src, target, resolve_include, include_stack);
                continue;
            }
        }
        collect_bind_element(child, target, resolve_include, include_stack);
    }
}

}

std::expected<UiDocument, UiError> parse_xml(std::string_view xml, IFatalError* fatal, const ViewModel* data_context,
        const UiIncludeResolver& resolve_include) {
    tinyxml2::XMLDocument doc;
    const tinyxml2::XMLError parsed = doc.Parse(xml.data(), xml.size());
    if (parsed != tinyxml2::XML_SUCCESS || doc.RootElement() == nullptr) {
        report(fatal, "invalid UI XML");
        return std::unexpected(UiError::InvalidMarkup);
    }

    std::vector<std::string> include_stack;
    auto root = parse_element(doc.RootElement(), fatal, data_context, false, resolve_include, include_stack);
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

std::expected<BindBinder, UiError> scan_bind_tree(std::string_view xml, const UiIncludeResolver& resolve_include) {
    const auto parsed = parse_xml(xml, nullptr, nullptr, resolve_include);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS || doc.RootElement() == nullptr) {
        return std::unexpected(UiError::InvalidMarkup);
    }

    BindBinder binder;
    std::vector<std::string> include_stack;
    collect_bind_element(doc.RootElement(), binder, resolve_include, include_stack);
    return binder;
}

}
