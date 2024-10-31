#include "css_length.h"

#include <engine/resources/asset_id.h>
#include <engine/ui/stylesheet.h>

#include <cctype>
#include <cstdlib>
#include <optional>
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

std::string to_lower(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool is_known_property(std::string_view name) {
    static constexpr std::string_view kKnown[] = {
            "color",
            "background",
            "opacity",
            "visibility",
            "width",
            "height",
            "min-width",
            "min-height",
            "padding",
            "margin",
            "gap",
            "flex-direction",
            "align-items",
            "justify-content",
            "text-align",
            "background-image",
            "border-radius",
            "border-width",
            "border-color",
            "font-size",
            "font-family",
            "animation-name",
            "animation-duration",
    };
    for (const std::string_view known : kKnown) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

bool is_length_property(std::string_view name) {
    return name == "width" || name == "height" || name == "min-width" || name == "min-height" || name == "padding" ||
            name == "margin" || name == "gap" || name == "font-size" || name == "border-radius" ||
            name == "border-width";
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

void skip_whitespace_and_comments(std::string_view css, std::size_t& i, std::size_t limit) {
    while (i < limit) {
        const unsigned char ch = static_cast<unsigned char>(css[i]);
        if (std::isspace(ch) != 0) {
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < limit && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < limit && !(css[i] == '*' && css[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < limit) {
                i += 2;
            } else {
                i = limit;
            }
            continue;
        }
        break;
    }
}

void skip_whitespace_and_comments(std::string_view css, std::size_t& i) {
    skip_whitespace_and_comments(css, i, css.size());
}

std::optional<CssSelector> parse_simple_selector(std::string_view raw) {
    const std::string_view selector = trim(raw);
    if (selector.empty()) {
        return std::nullopt;
    }

    CssSelector parsed;
    std::string_view body = selector;
    const auto colon = body.find(':');
    if (colon != std::string_view::npos) {
        parsed.pseudo = std::string(body.substr(colon + 1));
        body = body.substr(0, colon);
    }
    if (body.empty() && parsed.pseudo.empty()) {
        return std::nullopt;
    }

    if (body.starts_with('.')) {
        parsed.type = CssSelectorType::Class;
        parsed.class_name = std::string(body.substr(1));
        return parsed;
    }
    if (body.starts_with('#')) {
        parsed.type = CssSelectorType::Id;
        parsed.id = std::string(body.substr(1));
        return parsed;
    }
    const auto dot = body.find('.');
    if (dot != std::string_view::npos) {
        parsed.type = CssSelectorType::ElementClass;
        parsed.element = std::string(body.substr(0, dot));
        parsed.class_name = std::string(body.substr(dot + 1));
        return parsed;
    }
    parsed.type = CssSelectorType::Element;
    parsed.element = std::string(body);
    return parsed;
}

bool parse_selector_chain(std::string_view raw, CssRule& rule, std::vector<std::string>& warnings) {
    const std::string_view selector = trim(raw);
    if (selector.empty()) {
        return false;
    }
    if (selector.find(',') != std::string_view::npos || selector.find('+') != std::string_view::npos ||
            selector.find('~') != std::string_view::npos) {
        warnings.emplace_back("unsupported combinator in selector: " + std::string(selector));
        return false;
    }

    std::vector<CssSelector> compounds;
    std::vector<CssCombinator> combinators;
    std::size_t i = 0;
    const auto skip_ws = [&] {
        while (i < selector.size() && std::isspace(static_cast<unsigned char>(selector[i])) != 0) {
            ++i;
        }
    };

    skip_ws();
    while (i < selector.size()) {
        bool has_combinator = false;
        CssCombinator combinator = CssCombinator::Descendant;
        if (selector[i] == '>') {
            if (compounds.empty()) {
                warnings.emplace_back("unsupported combinator in selector: " + std::string(selector));
                return false;
            }
            combinator = CssCombinator::Child;
            has_combinator = true;
            ++i;
            skip_ws();
        } else if (!compounds.empty()) {
            combinator = CssCombinator::Descendant;
            has_combinator = true;
        }
        if (i >= selector.size() || selector[i] == '>') {
            warnings.emplace_back("unsupported combinator in selector: " + std::string(selector));
            return false;
        }

        const std::size_t begin = i;
        while (i < selector.size() && std::isspace(static_cast<unsigned char>(selector[i])) == 0 &&
                selector[i] != '>') {
            ++i;
        }
        auto compound = parse_simple_selector(selector.substr(begin, i - begin));
        if (!compound) {
            return false;
        }
        if (has_combinator) {
            combinators.push_back(combinator);
        }
        compounds.push_back(std::move(*compound));
        skip_ws();
    }

    if (compounds.empty()) {
        return false;
    }
    rule.selector = std::move(compounds.back());
    compounds.pop_back();
    rule.ancestors = std::move(compounds);
    rule.combinators = std::move(combinators);
    return true;
}

void parse_declarations(std::string_view body, std::vector<CssDeclaration>& declarations, std::vector<std::string>& warnings) {
    std::size_t i = 0;
    while (i < body.size()) {
        const auto semi = body.find(';', i);
        const std::string_view chunk =
                trim(body.substr(i, semi == std::string_view::npos ? std::string_view::npos : semi - i));
        i = semi == std::string_view::npos ? body.size() : semi + 1;
        if (chunk.empty()) {
            continue;
        }
        const auto colon = chunk.find(':');
        if (colon == std::string_view::npos) {
            warnings.emplace_back("invalid CSS declaration: " + std::string(chunk));
            continue;
        }
        CssDeclaration decl;
        decl.property = to_lower(trim(chunk.substr(0, colon)));
        decl.value = std::string(trim(chunk.substr(colon + 1)));
        if (decl.property.empty()) {
            continue;
        }
        if (!is_known_property(decl.property)) {
            warnings.emplace_back("unknown CSS property: " + decl.property);
        } else if (decl.property == "background-image") {
            const std::string_view value = trim(decl.value);
            if (value != "none" && !AssetId::parse(value)) {
                warnings.emplace_back(
                        "background-image value must be none or a 32-hex AssetId, not a filename: " + decl.value);
            }
        } else if (is_length_property(decl.property)) {
            const bool padding_like = decl.property == "padding" || decl.property == "margin";
            if (css_length::contains_var(decl.value)) {
                warnings.emplace_back("var() is not supported");
                continue;
            }
            const bool has_calc = decl.value.find("calc") != std::string::npos;
            if (has_calc) {
                const bool ok = padding_like ? css_length::parse_insets(decl.value).has_value()
                                             : css_length::parse_length(decl.value).has_value();
                if (!ok) {
                    warnings.emplace_back("invalid calc()");
                    continue;
                }
            }
        }
        declarations.push_back(std::move(decl));
    }
}

bool consume_curly_block(
        std::string_view css, std::size_t& i, std::size_t limit, std::size_t& body_begin, std::size_t& body_end) {
    if (i >= limit || css[i] != '{') {
        return false;
    }
    ++i;
    body_begin = i;
    int depth = 1;
    while (i < limit && depth > 0) {
        if (css[i] == '{') {
            ++depth;
        } else if (css[i] == '}') {
            --depth;
        }
        if (depth > 0) {
            ++i;
        }
    }
    if (i >= limit || css[i] != '}') {
        return false;
    }
    body_end = i;
    ++i;
    return true;
}

void skip_at_rule(std::string_view css, std::size_t& i, std::size_t limit) {
    while (i < limit && css[i] != ';' && css[i] != '{') {
        ++i;
    }
    if (i < limit && css[i] == ';') {
        ++i;
        return;
    }
    std::size_t body_begin = 0;
    std::size_t body_end = 0;
    if (i < limit && css[i] == '{') {
        if (!consume_curly_block(css, i, limit, body_begin, body_end)) {
            i = limit;
        }
    }
}

std::optional<MediaQuery> parse_media_query(std::string_view prelude) {
    const std::string_view query = trim(prelude);
    if (query.size() < 2 || query.front() != '(' || query.back() != ')') {
        return std::nullopt;
    }
    const std::string_view inner = trim(query.substr(1, query.size() - 2));
    const auto colon = inner.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string feature = to_lower(trim(inner.substr(0, colon)));
    const std::string_view value = trim(inner.substr(colon + 1));
    MediaQuery media;
    if (feature == "min-width") {
        media.feature = MediaFeature::MinWidth;
    } else if (feature == "min-height") {
        media.feature = MediaFeature::MinHeight;
    } else {
        return std::nullopt;
    }
    const std::string tmp(value);
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return std::nullopt;
    }
    const std::string_view suffix = trim(std::string_view(end));
    if (!suffix.empty() && suffix != "px") {
        return std::nullopt;
    }
    media.px = n;
    return media;
}

std::optional<float> parse_keyframe_offset(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "from") {
        return 0.0f;
    }
    if (value == "to") {
        return 1.0f;
    }
    if (value.empty() || value.back() != '%') {
        return std::nullopt;
    }
    const std::string tmp(value.substr(0, value.size() - 1));
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str() || end != tmp.c_str() + tmp.size()) {
        return std::nullopt;
    }
    return n / 100.0f;
}

bool parse_keyframes_body(std::string_view body, Keyframes& keyframes, std::vector<std::string>& warnings) {
    std::size_t i = 0;
    while (i < body.size()) {
        skip_whitespace_and_comments(body, i);
        if (i >= body.size()) {
            break;
        }
        const std::size_t sel_begin = i;
        while (i < body.size() && body[i] != '{') {
            ++i;
        }
        if (i >= body.size()) {
            return trim(body.substr(sel_begin)).empty();
        }
        const std::string_view selector = body.substr(sel_begin, i - sel_begin);
        std::size_t block_begin = 0;
        std::size_t block_end = 0;
        if (!consume_curly_block(body, i, body.size(), block_begin, block_end)) {
            return false;
        }
        const auto offset = parse_keyframe_offset(selector);
        if (!offset) {
            warnings.emplace_back("invalid keyframe selector");
            continue;
        }
        KeyframeStop stop;
        stop.offset = *offset;
        parse_declarations(body.substr(block_begin, block_end - block_begin), stop.declarations, warnings);
        keyframes.stops.push_back(std::move(stop));
    }
    return true;
}

bool parse_stylesheet_body(std::string_view css, std::size_t& i, std::size_t limit, Stylesheet& sheet,
        const std::optional<MediaQuery>& media, std::vector<std::string>& warnings);

bool parse_at_rule(std::string_view css, std::size_t& i, std::size_t limit, Stylesheet& sheet,
        const std::optional<MediaQuery>& parent_media, std::vector<std::string>& warnings) {
    ++i;
    const std::size_t name_begin = i;
    while (i < limit && is_ident_char(css[i])) {
        ++i;
    }
    const std::string name = to_lower(css.substr(name_begin, i - name_begin));
    skip_whitespace_and_comments(css, i, limit);
    const std::size_t prelude_begin = i;
    while (i < limit && css[i] != '{' && css[i] != ';') {
        ++i;
    }
    const std::string_view prelude = css.substr(prelude_begin, i - prelude_begin);

    if (name == "media") {
        std::size_t body_begin = 0;
        std::size_t body_end = 0;
        if (i >= limit || css[i] != '{' || !consume_curly_block(css, i, limit, body_begin, body_end)) {
            return false;
        }
        if (parent_media) {
            warnings.emplace_back("unknown media");
            return true;
        }
        const auto query = parse_media_query(prelude);
        if (!query) {
            warnings.emplace_back("unknown media");
            return true;
        }
        std::size_t inner = body_begin;
        return parse_stylesheet_body(css, inner, body_end, sheet, query, warnings);
    }

    if (name == "keyframes") {
        std::size_t body_begin = 0;
        std::size_t body_end = 0;
        if (i >= limit || css[i] != '{' || !consume_curly_block(css, i, limit, body_begin, body_end)) {
            return false;
        }
        const std::string_view kf_name = trim(prelude);
        if (kf_name.empty()) {
            warnings.emplace_back("unsupported at-rule");
            return true;
        }
        Keyframes keyframes;
        keyframes.name = std::string(kf_name);
        if (!parse_keyframes_body(css.substr(body_begin, body_end - body_begin), keyframes, warnings)) {
            return false;
        }
        sheet.keyframes.push_back(std::move(keyframes));
        return true;
    }

    warnings.emplace_back("unsupported at-rule");
    skip_at_rule(css, i, limit);
    return true;
}

bool parse_stylesheet_body(std::string_view css, std::size_t& i, std::size_t limit, Stylesheet& sheet,
        const std::optional<MediaQuery>& media, std::vector<std::string>& warnings) {
    while (i < limit) {
        skip_whitespace_and_comments(css, i, limit);
        if (i >= limit) {
            break;
        }
        if (css[i] == '}') {
            break;
        }
        if (css[i] == '@') {
            if (!parse_at_rule(css, i, limit, sheet, media, warnings)) {
                return false;
            }
            continue;
        }

        const std::size_t selector_begin = i;
        while (i < limit && css[i] != '{') {
            ++i;
        }
        if (i >= limit) {
            const std::string_view rest = trim(css.substr(selector_begin, limit - selector_begin));
            return rest.empty();
        }

        const std::string_view selector_text = css.substr(selector_begin, i - selector_begin);
        std::size_t body_begin = 0;
        std::size_t body_end = 0;
        if (!consume_curly_block(css, i, limit, body_begin, body_end)) {
            return false;
        }

        CssRule rule;
        rule.media = media;
        if (!parse_selector_chain(selector_text, rule, warnings)) {
            continue;
        }
        parse_declarations(css.substr(body_begin, body_end - body_begin), rule.declarations, warnings);
        sheet.rules.push_back(std::move(rule));
    }
    return true;
}

}

std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings) {
    Stylesheet sheet;
    std::size_t i = 0;
    if (!parse_stylesheet_body(css, i, css.size(), sheet, std::nullopt, warnings)) {
        return std::unexpected(CssError::InvalidSyntax);
    }
    return sheet;
}

}
