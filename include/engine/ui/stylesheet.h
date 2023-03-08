#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

enum class CssError {
    InvalidSyntax,
};

enum class CssSelectorType {
    Element,
    Class,
    Id,
    ElementClass,
};

struct CssSelector {
    CssSelectorType type = CssSelectorType::Element;
    std::string element;
    std::string class_name;
    std::string id;
    std::string pseudo;
};

struct CssDeclaration {
    std::string property;
    std::string value;
};

struct CssRule {
    CssSelector selector;
    std::vector<CssDeclaration> declarations;
};

struct Stylesheet {
    std::vector<CssRule> rules;
};

[[nodiscard]] std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings);

}
