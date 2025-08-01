#pragma once

#include <cstdint>
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

// Bumped once per freshly default-constructed Stylesheet, preserved (not re-bumped) by copy/move
// since those carry the same content forward. Element's compute_style() cache (document.h's
// StyleCacheEntry) keys on this alongside the Stylesheet* itself: some reload paths (e.g.
// systems.cpp's run_bind merging extra_stylesheets) move-assign a freshly parsed Stylesheet into
// an already-engaged std::optional<Stylesheet> living inside a long-lived UiInstance component,
// which leaves the pointer identical across a real content change — generation catches that.
[[nodiscard]] inline std::uint64_t next_stylesheet_generation() noexcept {
    static std::uint64_t counter = 0;
    return ++counter;
}

struct Stylesheet {
    std::uint64_t generation = next_stylesheet_generation();
    std::vector<CssRule> rules;
    std::vector<Keyframes> keyframes;
};

[[nodiscard]] std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings);

}
