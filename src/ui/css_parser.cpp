#include <engine/resources/asset_id.h>
#include <engine/ui/stylesheet.h>

#include <cctype>
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
    };
    for (const std::string_view known : kKnown) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

void skip_whitespace_and_comments(std::string_view css, std::size_t& i) {
    while (i < css.size()) {
        const unsigned char ch = static_cast<unsigned char>(css[i]);
        if (std::isspace(ch) != 0) {
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < css.size()) {
                i += 2;
            } else {
                i = css.size();
            }
            continue;
        }
        break;
    }
}

std::optional<CssSelector> parse_selector(std::string_view raw, std::vector<std::string>& warnings) {
    const std::string_view selector = trim(raw);
    if (selector.empty()) {
        return std::nullopt;
    }
    if (selector.find(',') != std::string_view::npos || selector.find('>') != std::string_view::npos ||
            selector.find(' ') != std::string_view::npos || selector.find('\t') != std::string_view::npos) {
        warnings.emplace_back("unsupported combinator in selector: " + std::string(selector));
        return std::nullopt;
    }

    CssSelector parsed;
    std::string_view body = selector;
    const auto colon = body.find(':');
    if (colon != std::string_view::npos) {
        parsed.pseudo = std::string(body.substr(colon + 1));
        body = body.substr(0, colon);
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

void parse_declarations(std::string_view body, CssRule& rule, std::vector<std::string>& warnings) {
    std::size_t i = 0;
    while (i < body.size()) {
        const auto semi = body.find(';', i);
        const std::string_view chunk = trim(body.substr(i, semi == std::string_view::npos ? std::string_view::npos : semi - i));
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
        }
        rule.declarations.push_back(std::move(decl));
    }
}

}

std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings) {
    Stylesheet sheet;
    std::size_t i = 0;
    while (i < css.size()) {
        skip_whitespace_and_comments(css, i);
        if (i >= css.size()) {
            break;
        }

        if (css[i] == '@') {
            warnings.emplace_back("unsupported at-rule");
            while (i < css.size() && css[i] != ';' && css[i] != '{') {
                ++i;
            }
            if (i < css.size() && css[i] == ';') {
                ++i;
                continue;
            }
            if (i < css.size() && css[i] == '{') {
                int depth = 1;
                ++i;
                while (i < css.size() && depth > 0) {
                    if (css[i] == '{') {
                        ++depth;
                    } else if (css[i] == '}') {
                        --depth;
                    }
                    ++i;
                }
            }
            continue;
        }

        const std::size_t selector_begin = i;
        while (i < css.size() && css[i] != '{') {
            ++i;
        }
        if (i >= css.size()) {
            const std::string_view rest = trim(css.substr(selector_begin));
            if (rest.empty()) {
                break;
            }
            return std::unexpected(CssError::InvalidSyntax);
        }

        const std::string_view selector_text = css.substr(selector_begin, i - selector_begin);
        ++i;
        const std::size_t body_begin = i;
        int depth = 1;
        while (i < css.size() && depth > 0) {
            if (css[i] == '{') {
                ++depth;
            } else if (css[i] == '}') {
                --depth;
            }
            if (depth > 0) {
                ++i;
            }
        }
        if (i >= css.size() || css[i] != '}') {
            return std::unexpected(CssError::InvalidSyntax);
        }
        const std::string_view body = css.substr(body_begin, i - body_begin);
        ++i;

        auto selector = parse_selector(selector_text, warnings);
        if (!selector) {
            continue;
        }
        CssRule rule;
        rule.selector = std::move(*selector);
        parse_declarations(body, rule, warnings);
        sheet.rules.push_back(std::move(rule));
    }
    return sheet;
}

}
