#pragma once

#include <expected>
#include <optional>
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

enum class CssCombinator {
    Descendant,
    Child,
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

enum class MediaFeature {
    MinWidth,
    MinHeight,
};

struct MediaQuery {
    MediaFeature feature = MediaFeature::MinWidth;
    float px = 0.0f;
};

struct CssRule {
    CssSelector selector;
    std::vector<CssSelector> ancestors;
    std::vector<CssCombinator> combinators;
    std::vector<CssDeclaration> declarations;
    std::optional<MediaQuery> media;
};

struct KeyframeStop {
    float offset = 0.0f;
    std::vector<CssDeclaration> declarations;
};

struct Keyframes {
    std::string name;
    std::vector<KeyframeStop> stops;
};

struct Stylesheet {
    std::vector<CssRule> rules;
    std::vector<Keyframes> keyframes;
};

[[nodiscard]] std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings);

}
