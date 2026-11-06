#include "shader_cook/parse_shader.h"

#include "shader_cook/slang_compiler.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include <tinyxml2.h>

namespace engine {
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

bool extract_vertex_fragment(const tinyxml2::XMLElement* parent, std::string& out_vs, std::string& out_fs) {
    if (parent == nullptr) {
        return false;
    }
    const tinyxml2::XMLElement* const vertex = parent->FirstChildElement("vertex");
    const tinyxml2::XMLElement* const fragment = parent->FirstChildElement("fragment");
    if (vertex == nullptr || fragment == nullptr) {
        return false;
    }
    const char* const vs = vertex->GetText();
    const char* const fs = fragment->GetText();
    if (vs == nullptr || fs == nullptr) {
        return false;
    }
    out_vs = std::string(trim(vs));
    out_fs = std::string(trim(fs));
    return !out_vs.empty() && !out_fs.empty();
}

// One level. The included file is source text, not XML, so it may contain `<`.
std::expected<std::string, std::string> expand_shader_includes(
        std::string source, const std::filesystem::path& dir) {
    constexpr std::string_view token = "#include \"";
    std::string out;
    out.reserve(source.size());
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const std::size_t pos = source.find(token, cursor);
        if (pos == std::string::npos) {
            out.append(source, cursor, std::string::npos);
            break;
        }
        out.append(source, cursor, pos - cursor);
        const std::size_t end = source.find('"', pos + token.size());
        if (end == std::string::npos) {
            return std::unexpected("shader include is missing a closing quote");
        }
        const std::string name = source.substr(pos + token.size(), end - (pos + token.size()));
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
                name.find("..") != std::string::npos) {
            return std::unexpected("shader include must be a file name in the shader directory");
        }
        if (dir.empty()) {
            return std::unexpected("shader include \"" + name + "\" needs the shader's directory");
        }
        std::ifstream in(dir / name);
        if (!in) {
            return std::unexpected("shader include not found: " + name);
        }
        out.append(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        cursor = end + 1;
    }
    return out;
}

}

std::expected<render::ShaderDesc, std::string> parse_shader(
        std::string_view xml, const std::filesystem::path& include_dir) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return std::unexpected("invalid shader XML");
    }
    const tinyxml2::XMLElement* const root = doc.RootElement();
    if (root == nullptr) {
        return std::unexpected("shader XML has no root element");
    }

    const char* const lang_attr = root->Attribute("lang");
    const bool is_slang_root = (lang_attr != nullptr && std::string_view(lang_attr) == "slang") ||
            std::string_view(root->Name()) == "slang";
    const tinyxml2::XMLElement* const slang_child = root->FirstChildElement("slang");
    const tinyxml2::XMLElement* const glsl_child = root->FirstChildElement("glsl");

    const char* const vs_entry_attr = (slang_child && slang_child->Attribute("vertex"))
            ? slang_child->Attribute("vertex")
            : root->Attribute("vertex");
    const char* const fs_entry_attr = (slang_child && slang_child->Attribute("fragment"))
            ? slang_child->Attribute("fragment")
            : root->Attribute("fragment");
    const std::string_view vs_entry = vs_entry_attr ? std::string_view(vs_entry_attr) : "vertexMain";
    const std::string_view fs_entry = fs_entry_attr ? std::string_view(fs_entry_attr) : "fragmentMain";

    render::ShaderDesc desc;
    bool has_slang = false;

#ifdef ENGINE_WITH_SLANG
    if (slang_child != nullptr) {
        has_slang = true;
        std::string stage_vs;
        std::string stage_fs;
        if (extract_vertex_fragment(slang_child, stage_vs, stage_fs)) {
            auto vs = expand_shader_includes(std::move(stage_vs), include_dir);
            auto fs = expand_shader_includes(std::move(stage_fs), include_dir);
            if (!vs) {
                return std::unexpected(vs.error());
            }
            if (!fs) {
                return std::unexpected(fs.error());
            }
            auto compiled = render::compile_slang_shader_stages(*vs, *fs, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        } else {
            const char* const slang_text = slang_child->GetText();
            if (slang_text == nullptr || trim(slang_text).empty()) {
                return std::unexpected("empty <slang> element in shader");
            }
            auto slang_src = expand_shader_includes(std::string(trim(slang_text)), include_dir);
            if (!slang_src) {
                return std::unexpected(slang_src.error());
            }
            auto compiled = render::compile_slang_shader(*slang_src, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        }
    } else if (is_slang_root) {
        has_slang = true;
        std::string stage_vs;
        std::string stage_fs;
        if (extract_vertex_fragment(root, stage_vs, stage_fs)) {
            auto vs = expand_shader_includes(std::move(stage_vs), include_dir);
            auto fs = expand_shader_includes(std::move(stage_fs), include_dir);
            if (!vs) {
                return std::unexpected(vs.error());
            }
            if (!fs) {
                return std::unexpected(fs.error());
            }
            auto compiled = render::compile_slang_shader_stages(*vs, *fs, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        } else {
            const char* const slang_text = root->GetText();
            if (slang_text == nullptr || trim(slang_text).empty()) {
                return std::unexpected("empty Slang shader source");
            }
            auto slang_src = expand_shader_includes(std::string(trim(slang_text)), include_dir);
            if (!slang_src) {
                return std::unexpected(slang_src.error());
            }
            auto compiled = render::compile_slang_shader(*slang_src, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        }
    }
#else
    if (slang_child != nullptr || is_slang_root) {
        has_slang = true;
        if (glsl_child == nullptr) {
            return std::unexpected("Slang compiler is not available in this build and no <glsl> fallback was provided");
        }
    }
#endif

    if (glsl_child != nullptr) {
        std::string glsl_vs;
        std::string glsl_fs;
        if (!extract_vertex_fragment(glsl_child, glsl_vs, glsl_fs)) {
            return std::unexpected("<glsl> override requires non-empty <vertex> and <fragment>");
        }
        auto vs = expand_shader_includes(std::move(glsl_vs), include_dir);
        auto fs = expand_shader_includes(std::move(glsl_fs), include_dir);
        if (!vs) {
            return std::unexpected(vs.error());
        }
        if (!fs) {
            return std::unexpected(fs.error());
        }
        desc.vertex_src = std::move(*vs);
        desc.fragment_src = std::move(*fs);
        return desc;
    }

    if (has_slang) {
        return desc;
    }

    std::string legacy_vs;
    std::string legacy_fs;
    if (!extract_vertex_fragment(root, legacy_vs, legacy_fs)) {
        return std::unexpected("shader requires <slang>, lang=\"slang\", or <vertex> and <fragment>");
    }
    auto vs = expand_shader_includes(std::move(legacy_vs), include_dir);
    auto fs = expand_shader_includes(std::move(legacy_fs), include_dir);
    if (!vs) {
        return std::unexpected(vs.error());
    }
    if (!fs) {
        return std::unexpected(fs.error());
    }
    desc.vertex_src = std::move(*vs);
    desc.fragment_src = std::move(*fs);
    return desc;
}

std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml, const std::filesystem::path& include_dir) {
    auto parsed = parse_shader(xml, include_dir);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(*parsed);
}

}
