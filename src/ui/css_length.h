#pragma once

#include <engine/ui/document.h>

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {
namespace css_length {
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

bool is_ident_cont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

std::size_t emit_literal(std::vector<CalcNode>& nodes, const Length& length) {
    CalcNode node;
    node.kind = CalcNode::Kind::Literal;
    node.value = length.value;
    node.unit = length.unit;
    nodes.push_back(node);
    return nodes.size() - 1;
}

std::size_t emit_binary(std::vector<CalcNode>& nodes, CalcOp op, std::size_t left, std::size_t right) {
    CalcNode node;
    node.kind = CalcNode::Kind::Binary;
    node.op = op;
    node.left = left;
    node.right = right;
    nodes.push_back(node);
    return nodes.size() - 1;
}

struct Parser {
    std::string_view text;
    std::size_t i = 0;

    void skip_ws() {
        while (i < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (std::isspace(ch) != 0) {
                ++i;
                continue;
            }
            if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                    ++i;
                }
                if (i + 1 < text.size()) {
                    i += 2;
                } else {
                    i = text.size();
                }
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool at_space() const {
        return i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0;
    }

    [[nodiscard]] bool starts_with_ident(std::string_view ident) const {
        if (i + ident.size() > text.size()) {
            return false;
        }
        if (text.substr(i, ident.size()) != ident) {
            return false;
        }
        if (i + ident.size() < text.size() && is_ident_cont(text[i + ident.size()])) {
            return false;
        }
        return true;
    }
};

std::optional<Length> parse_number_unit(Parser& parser) {
    parser.skip_ws();
    if (parser.i >= parser.text.size()) {
        return std::nullopt;
    }
    const std::string tmp(parser.text.substr(parser.i));
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return std::nullopt;
    }
    const std::size_t number_len = static_cast<std::size_t>(end - tmp.c_str());
    std::size_t suffix_end = number_len;
    while (suffix_end < tmp.size() && std::isspace(static_cast<unsigned char>(tmp[suffix_end])) == 0 &&
            tmp[suffix_end] != ')' && tmp[suffix_end] != '*' && tmp[suffix_end] != '/' && tmp[suffix_end] != '+' &&
            tmp[suffix_end] != '-') {
        ++suffix_end;
    }
    const std::string_view suffix = trim(std::string_view(tmp.data() + number_len, suffix_end - number_len));
    Length length;
    length.value = n;
    if (suffix.empty() || suffix == "px") {
        length.unit = LengthUnit::Px;
    } else if (suffix == "%") {
        length.unit = LengthUnit::Percent;
    } else if (suffix == "em") {
        length.unit = LengthUnit::Em;
    } else {
        return std::nullopt;
    }
    parser.i += suffix_end;
    return length;
}

bool parse_sum(Parser& parser, std::vector<CalcNode>& nodes, std::size_t& root);

bool parse_value(Parser& parser, std::vector<CalcNode>& nodes, std::size_t& root) {
    parser.skip_ws();
    if (parser.i >= parser.text.size()) {
        return false;
    }
    if (parser.text[parser.i] == '(') {
        ++parser.i;
        if (!parse_sum(parser, nodes, root)) {
            return false;
        }
        parser.skip_ws();
        if (parser.i >= parser.text.size() || parser.text[parser.i] != ')') {
            return false;
        }
        ++parser.i;
        return true;
    }
    const auto literal = parse_number_unit(parser);
    if (!literal) {
        return false;
    }
    root = emit_literal(nodes, *literal);
    return true;
}

bool parse_product(Parser& parser, std::vector<CalcNode>& nodes, std::size_t& root) {
    if (!parse_value(parser, nodes, root)) {
        return false;
    }
    for (;;) {
        const std::size_t before = parser.i;
        parser.skip_ws();
        if (parser.i >= parser.text.size()) {
            parser.i = before;
            break;
        }
        const char ch = parser.text[parser.i];
        if (ch != '*' && ch != '/') {
            parser.i = before;
            break;
        }
        const CalcOp op = ch == '*' ? CalcOp::Mul : CalcOp::Div;
        ++parser.i;
        std::size_t rhs = 0;
        if (!parse_value(parser, nodes, rhs)) {
            return false;
        }
        root = emit_binary(nodes, op, root, rhs);
    }
    return true;
}

bool parse_sum(Parser& parser, std::vector<CalcNode>& nodes, std::size_t& root) {
    if (!parse_product(parser, nodes, root)) {
        return false;
    }
    for (;;) {
        if (!parser.at_space()) {
            break;
        }
        parser.skip_ws();
        if (parser.i >= parser.text.size()) {
            break;
        }
        const char ch = parser.text[parser.i];
        if (ch != '+' && ch != '-') {
            break;
        }
        if (parser.i + 1 >= parser.text.size() ||
                std::isspace(static_cast<unsigned char>(parser.text[parser.i + 1])) == 0) {
            return false;
        }
        const CalcOp op = ch == '+' ? CalcOp::Add : CalcOp::Sub;
        ++parser.i;
        std::size_t rhs = 0;
        if (!parse_product(parser, nodes, rhs)) {
            return false;
        }
        root = emit_binary(nodes, op, root, rhs);
    }
    return true;
}

std::optional<Length> parse_calc(Parser& parser) {
    parser.skip_ws();
    if (!parser.starts_with_ident("calc")) {
        return std::nullopt;
    }
    parser.i += 4;
    parser.skip_ws();
    if (parser.i >= parser.text.size() || parser.text[parser.i] != '(') {
        return std::nullopt;
    }
    ++parser.i;
    std::vector<CalcNode> nodes;
    std::size_t root = 0;
    if (!parse_sum(parser, nodes, root)) {
        return std::nullopt;
    }
    parser.skip_ws();
    if (parser.i >= parser.text.size() || parser.text[parser.i] != ')') {
        return std::nullopt;
    }
    ++parser.i;
    if (nodes.empty() || root + 1 != nodes.size()) {
        return std::nullopt;
    }
    Length length;
    length.calc = std::move(nodes);
    return length;
}

std::optional<Length> parse_one(Parser& parser) {
    parser.skip_ws();
    if (parser.starts_with_ident("calc")) {
        return parse_calc(parser);
    }
    if (parser.starts_with_ident("var")) {
        return std::nullopt;
    }
    return parse_number_unit(parser);
}

} // namespace

[[nodiscard]] inline bool contains_var(std::string_view raw) {
    const std::string_view value = trim(raw);
    return value.find("var(") != std::string_view::npos;
}

[[nodiscard]] inline std::optional<Length> parse_length(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty() || contains_var(value)) {
        return std::nullopt;
    }
    Parser parser{value, 0};
    auto length = parse_one(parser);
    parser.skip_ws();
    if (!length || parser.i != parser.text.size()) {
        return std::nullopt;
    }
    return length;
}

[[nodiscard]] inline std::optional<LengthInsets> parse_insets(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty() || contains_var(value)) {
        return std::nullopt;
    }
    Parser parser{value, 0};
    Length parts[4] = {};
    int count = 0;
    while (parser.i < parser.text.size() && count < 4) {
        parser.skip_ws();
        if (parser.i >= parser.text.size()) {
            break;
        }
        const auto length = parse_one(parser);
        if (!length) {
            return std::nullopt;
        }
        parts[count++] = *length;
    }
    parser.skip_ws();
    if (parser.i != parser.text.size() || count == 0) {
        return std::nullopt;
    }
    LengthInsets padding;
    if (count == 1) {
        padding.top = padding.right = padding.bottom = padding.left = parts[0];
    } else if (count == 2) {
        padding.top = padding.bottom = parts[0];
        padding.right = padding.left = parts[1];
    } else if (count == 3) {
        padding.top = parts[0];
        padding.right = padding.left = parts[1];
        padding.bottom = parts[2];
    } else {
        padding.top = parts[0];
        padding.right = parts[1];
        padding.bottom = parts[2];
        padding.left = parts[3];
    }
    return padding;
}

} // namespace css_length
} // namespace engine::ui
