#include <engine/resources/component_codegen.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace {

enum class TokenKind {
    Ident,
    StringLiteral,
    Symbol,
};

struct Token {
    TokenKind kind = TokenKind::Symbol;
    std::string text;
    std::size_t line = 1;
};

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_ident_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::vector<Token> tokenize_cpp(std::string_view src) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    std::size_t line = 1;
    bool line_start = true;

    while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            line_start = true;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
            continue;
        }
        if (c == '#' && line_start) {
            while (i < src.size()) {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    if (src[i + 1] == '\r' && i + 2 < src.size() && src[i + 2] == '\n') {
                        i += 3;
                        ++line;
                        continue;
                    }
                    if (src[i + 1] == '\n') {
                        i += 2;
                        ++line;
                        continue;
                    }
                }
                if (src[i] == '\n') {
                    break;
                }
                ++i;
            }
            continue;
        }
        line_start = false;

        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            i += 2;
            while (i < src.size() && src[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') {
                    ++line;
                }
                ++i;
            }
            if (i + 1 < src.size()) {
                i += 2;
            } else {
                i = src.size();
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            const std::size_t start_line = line;
            std::string content;
            ++i;
            while (i < src.size() && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    content.push_back(src[i + 1]);
                    i += 2;
                    continue;
                }
                if (src[i] == '\n') {
                    ++line;
                }
                content.push_back(src[i]);
                ++i;
            }
            if (i < src.size()) {
                ++i;
            }
            if (quote == '"') {
                tokens.push_back(Token{TokenKind::StringLiteral, std::move(content), start_line});
            }
            continue;
        }
        if (is_ident_start(c)) {
            const std::size_t start = i;
            ++i;
            while (i < src.size() && is_ident_continue(src[i])) {
                ++i;
            }
            tokens.push_back(Token{TokenKind::Ident, std::string(src.substr(start, i - start)), line});
            continue;
        }
        if (c == ':' && i + 1 < src.size() && src[i + 1] == ':') {
            tokens.push_back(Token{TokenKind::Symbol, "::", line});
            i += 2;
            continue;
        }
        tokens.push_back(Token{TokenKind::Symbol, std::string(1, c), line});
        ++i;
    }

    return tokens;
}

std::string pascal_to_snake(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 4);
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (std::isupper(ch) != 0) {
            if (i > 0) {
                const unsigned char prev = static_cast<unsigned char>(name[i - 1]);
                const bool prev_lower = std::islower(prev) != 0;
                const bool next_lower =
                        (i + 1 < name.size()) && (std::islower(static_cast<unsigned char>(name[i + 1])) != 0);
                if (prev_lower || ( std::isupper(prev) != 0 && next_lower)) {
                    out.push_back('_');
                }
            }
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

bool is_skipped_statement_keyword(std::string_view word) {
    return word == "static" || word == "constexpr" || word == "consteval" || word == "using" ||
            word == "typedef" || word == "friend" || word == "enum" || word == "struct" || word == "class" ||
            word == "template" || word == "static_assert" || word == "operator";
}

std::expected<void, CodegenError> extract_struct_fields(
        const std::vector<Token>& tokens, std::size_t body_start, std::size_t body_end,
        std::string_view include_path, std::string_view struct_name, std::vector<std::string>& out_fields) {
    std::size_t i = body_start;
    while (i < body_end) {
        if (tokens[i].kind == TokenKind::Ident &&
                (tokens[i].text == "public" || tokens[i].text == "private" || tokens[i].text == "protected") &&
                i + 1 < body_end && tokens[i + 1].text == ":") {
            i += 2;
            continue;
        }
        if (tokens[i].text == ";") {
            ++i;
            continue;
        }

        const std::size_t stmt_start = i;
        int brace_depth = 0;
        int paren_depth = 0;
        int bracket_depth = 0;
        bool saw_top_paren = false;
        std::optional<std::size_t> init_or_end_pos;

        while (i < body_end) {
            const std::string_view sym = tokens[i].text;
            if (sym == "(") {
                if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
                    saw_top_paren = true;
                }
                ++paren_depth;
            } else if (sym == ")") {
                if (paren_depth > 0) {
                    --paren_depth;
                }
            } else if (sym == "[") {
                ++bracket_depth;
            } else if (sym == "]") {
                if (bracket_depth > 0) {
                    --bracket_depth;
                }
            } else if (sym == "{") {
                if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0 && !init_or_end_pos.has_value()) {
                    init_or_end_pos = i;
                }
                ++brace_depth;
            } else if (sym == "}") {
                if (brace_depth > 0) {
                    --brace_depth;
                    if (brace_depth == 0 && saw_top_paren) {
                        ++i;
                        break;
                    }
                }
            } else if (sym == "=" && brace_depth == 0 && paren_depth == 0 && bracket_depth == 0 &&
                       !init_or_end_pos.has_value()) {
                init_or_end_pos = i;
            } else if (sym == ";" && brace_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
                if (!init_or_end_pos.has_value()) {
                    init_or_end_pos = i;
                }
                ++i;
                break;
            }
            ++i;
        }

        if (saw_top_paren) {
            continue;
        }

        bool skip = false;
        for (std::size_t k = stmt_start; k < init_or_end_pos.value_or(i); ++k) {
            if (tokens[k].kind == TokenKind::Ident && is_skipped_statement_keyword(tokens[k].text)) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }

        if (!init_or_end_pos.has_value() || *init_or_end_pos <= stmt_start) {
            return std::unexpected(CodegenError{
                    CodegenErrorKind::ComponentCodegen,
                    std::format("{}:{}: malformed member in WIND_COMPONENT struct {}", include_path,
                            tokens[stmt_start].line, struct_name)});
        }

        const Token& name_tok = tokens[*init_or_end_pos - 1];
        if (name_tok.kind != TokenKind::Ident || *init_or_end_pos - 1 == stmt_start) {
            return std::unexpected(CodegenError{
                    CodegenErrorKind::ComponentCodegen,
                    std::format("{}:{}: could not determine field name in WIND_COMPONENT struct {}", include_path,
                            name_tok.line, struct_name)});
        }
        out_fields.push_back(name_tok.text);
    }
    return {};
}

bool is_header_file(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".h" || ext == ".hpp";
}

} // namespace

std::string ScannedComponent::qualified_name() const {
    std::string out;
    for (const std::string& ns : namespaces) {
        out += ns;
        out += "::";
    }
    out += struct_name;
    return out;
}

std::expected<std::vector<ScannedComponent>, CodegenError> scan_components_in_text(
        std::string_view source_text, std::string_view include_path) {
    const std::vector<Token> tokens = tokenize_cpp(source_text);
    std::vector<ScannedComponent> results;

    std::vector<std::string> ns_stack;
    // Number of namespace segments pushed by each open `{` on the brace stack (0 for non-namespace `{`).
    std::vector<std::size_t> brace_ns_counts;

    std::size_t i = 0;
    while (i < tokens.size()) {
        const Token& tok = tokens[i];
        if (tok.kind == TokenKind::Ident && tok.text == "namespace") {
            std::size_t cursor = i + 1;
            std::vector<std::string> segments;
            while (cursor < tokens.size() && tokens[cursor].kind == TokenKind::Ident) {
                segments.push_back(tokens[cursor].text);
                ++cursor;
                if (cursor < tokens.size() && tokens[cursor].text == "::") {
                    ++cursor;
                } else {
                    break;
                }
            }
            if (cursor < tokens.size() && tokens[cursor].text == "{") {
                for (std::string& seg : segments) {
                    ns_stack.push_back(std::move(seg));
                }
                brace_ns_counts.push_back(segments.size());
                i = cursor + 1;
                continue;
            }
        }

        if (tok.text == "{") {
            brace_ns_counts.push_back(0);
            ++i;
            continue;
        }
        if (tok.text == "}") {
            if (!brace_ns_counts.empty()) {
                const std::size_t popped = brace_ns_counts.back();
                brace_ns_counts.pop_back();
                for (std::size_t k = 0; k < popped && !ns_stack.empty(); ++k) {
                    ns_stack.pop_back();
                }
            }
            ++i;
            continue;
        }

        if (tok.kind == TokenKind::Ident && (tok.text == "struct" || tok.text == "class") &&
                i + 1 < tokens.size() && tokens[i + 1].kind == TokenKind::Ident &&
                tokens[i + 1].text == "WIND_COMPONENT") {
            std::size_t cursor = i + 2;
            std::string custom_key;
            if (cursor + 2 < tokens.size() && tokens[cursor].text == "(" &&
                    tokens[cursor + 1].kind == TokenKind::StringLiteral && tokens[cursor + 2].text == ")") {
                custom_key = tokens[cursor + 1].text;
                cursor += 3;
            }
            if (cursor >= tokens.size() || tokens[cursor].kind != TokenKind::Ident) {
                return std::unexpected(CodegenError{
                        CodegenErrorKind::ComponentCodegen,
                        std::format("{}:{}: expected struct name after WIND_COMPONENT", include_path, tok.line)});
            }
            const std::string struct_name = tokens[cursor].text;
            ++cursor;
            if (cursor < tokens.size() && tokens[cursor].text == ";") {
                i = cursor + 1;
                continue;
            }
            while (cursor < tokens.size() && tokens[cursor].text != "{") {
                ++cursor;
            }
            if (cursor >= tokens.size()) {
                return std::unexpected(CodegenError{
                        CodegenErrorKind::ComponentCodegen,
                        std::format("{}:{}: missing '{{' for WIND_COMPONENT struct {}", include_path, tok.line,
                                struct_name)});
            }
            const std::size_t body_start = cursor + 1;
            int depth = 1;
            std::size_t scan = body_start;
            while (scan < tokens.size() && depth > 0) {
                if (tokens[scan].text == "{") {
                    ++depth;
                } else if (tokens[scan].text == "}") {
                    --depth;
                }
                if (depth > 0) {
                    ++scan;
                }
            }
            if (depth != 0) {
                return std::unexpected(CodegenError{
                        CodegenErrorKind::ComponentCodegen,
                        std::format("{}:{}: unclosed '{{' in WIND_COMPONENT struct {}", include_path, tok.line,
                                struct_name)});
            }

            ScannedComponent comp;
            comp.include_path = std::string(include_path);
            comp.namespaces = ns_stack;
            comp.struct_name = struct_name;
            comp.toml_key = custom_key.empty() ? pascal_to_snake(struct_name) : std::move(custom_key);

            if (auto ok = extract_struct_fields(tokens, body_start, scan, include_path, struct_name, comp.fields);
                    !ok) {
                return std::unexpected(std::move(ok.error()));
            }
            results.push_back(std::move(comp));
            i = scan + 1;
            continue;
        }

        ++i;
    }

    return results;
}

std::expected<std::vector<ScannedComponent>, CodegenError> scan_components_in_dirs(
        std::span<const std::filesystem::path> source_dirs) {
    std::vector<ScannedComponent> all;
    std::unordered_map<std::string, std::string> key_to_type;

    for (const std::filesystem::path& root : source_dirs) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
            continue;
        }

        std::vector<std::filesystem::path> files;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, options, ec)) {
            if (ec) {
                return std::unexpected(CodegenError{CodegenErrorKind::Io, ec.message()});
            }
            if (entry.is_regular_file() && is_header_file(entry.path())) {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const std::filesystem::path& file : files) {
            std::ifstream in(file);
            if (!in) {
                return std::unexpected(
                        CodegenError{CodegenErrorKind::Io, "failed to read " + file.generic_string()});
            }
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find("WIND_COMPONENT") == std::string::npos) {
                continue;
            }
            const std::string rel = file.lexically_relative(root).generic_string();
            auto scanned = scan_components_in_text(text, rel);
            if (!scanned) {
                return std::unexpected(std::move(scanned.error()));
            }
            for (ScannedComponent& comp : *scanned) {
                const std::string qname = comp.qualified_name();
                if (const auto it = key_to_type.find(comp.toml_key);
                        it != key_to_type.end() && it->second != qname) {
                    return std::unexpected(CodegenError{
                            CodegenErrorKind::Collision,
                            std::format("component key \"{}\" is used by both {} and {}", comp.toml_key,
                                    it->second, qname)});
                }
                key_to_type.emplace(comp.toml_key, qname);
                all.push_back(std::move(comp));
            }
        }
    }

    std::sort(all.begin(), all.end(), [](const ScannedComponent& a, const ScannedComponent& b) {
        return a.qualified_name() < b.qualified_name();
    });
    return all;
}

std::string emit_component_io_header(std::span<const ScannedComponent> components) {
    std::ostringstream os;
    os << "#pragma once\n\n";
    os << "#include <engine/ecs/world_serializer.h>\n";

    std::set<std::string> includes;
    for (const ScannedComponent& comp : components) {
        if (!comp.include_path.empty()) {
            includes.insert(comp.include_path);
        }
    }
    if (!includes.empty()) {
        os << '\n';
        for (const std::string& inc : includes) {
            os << "#include \"" << inc << "\"\n";
        }
    }

    os << "\nnamespace assets {\n\n";
    if (components.empty()) {
        os << "inline void register_game_components(engine::ecs::WorldSerializer&) {}\n\n";
    } else {
        os << "inline void register_game_components(engine::ecs::WorldSerializer& serializer) {\n";
        for (const ScannedComponent& comp : components) {
            const std::string qname = comp.qualified_name();
            os << "    serializer.register_component<" << qname << ">(\n";
            os << "            \"" << comp.toml_key << "\",\n";
            if (comp.fields.empty()) {
                os << "            [](engine::ecs::ComponentWriter&, const " << qname << "&) {},\n";
                os << "            [](engine::ecs::ComponentReader&, " << qname << "&) {});\n";
            } else {
                os << "            [](engine::ecs::ComponentWriter& out, const " << qname << "& c) {\n";
                for (const std::string& field : comp.fields) {
                    os << "                out.field(\"" << field << "\", c." << field << ");\n";
                }
                os << "            },\n";
                os << "            [](engine::ecs::ComponentReader& in, " << qname << "& c) {\n";
                for (const std::string& field : comp.fields) {
                    os << "                (void)in.field(\"" << field << "\", c." << field << ");\n";
                }
                os << "            });\n";
            }
        }
        os << "}\n\n";
    }
    os << "} // namespace assets\n";
    return os.str();
}

std::expected<void, CodegenError> component_codegen_write(
        std::span<const std::filesystem::path> source_dirs, const std::filesystem::path& output_file) {
    auto scanned = scan_components_in_dirs(source_dirs);
    if (!scanned) {
        return std::unexpected(std::move(scanned.error()));
    }

    if (output_file.has_parent_path()) {
        std::error_code create_ec;
        std::filesystem::create_directories(output_file.parent_path(), create_ec);
        if (create_ec) {
            return std::unexpected(CodegenError{CodegenErrorKind::Io, create_ec.message()});
        }
    }

    std::ofstream out(output_file, std::ios::trunc);
    if (!out) {
        return std::unexpected(
                CodegenError{CodegenErrorKind::Io, "failed to write " + output_file.generic_string()});
    }
    out << emit_component_io_header(*scanned);
    if (!out) {
        return std::unexpected(
                CodegenError{CodegenErrorKind::Io, "failed to write " + output_file.generic_string()});
    }
    return {};
}

} // namespace engine
