#include "importers.h"

#include "render/mesh_check.h"
#include "render/slang_compiler.h"

#include <cctype>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

} // namespace

// docs/tech/features/Assets.md#mesh-files
std::optional<render::MeshDesc> parse_mesh(std::string_view text) {
    render::MeshDesc desc;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.resize(comment);
        }
        const std::string_view trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        std::istringstream values{std::string(trimmed)};
        if (trimmed.front() == 'f') {
            std::string tag;
            values >> tag;
            if (tag != "f") {
                return std::nullopt;
            }
            for (int corner = 0; corner < 3; ++corner) {
                constexpr long long kMaxIndex = std::numeric_limits<std::uint32_t>::max();
                long long index = 0;
                if (!(values >> index) || index < 0 || index > kMaxIndex) {
                    return std::nullopt;
                }
                desc.indices.push_back(static_cast<std::uint32_t>(index));
            }
            std::string extra;
            if (values >> extra) {
                return std::nullopt;
            }
        } else {
            // 5 numbers: position, uv (normal +Z, white). 8 numbers: position, normal, uv (white). 12 numbers:
            // position, normal, uv, rgba color.
            std::vector<float> numbers;
            float number = 0.0f;
            while (values >> number) {
                numbers.push_back(number);
            }
            if (!values.eof() || (numbers.size() != 5 && numbers.size() != 8 && numbers.size() != 12)) {
                return std::nullopt;
            }
            render::MeshVertex vertex;
            vertex.position = {numbers[0], numbers[1], numbers[2]};
            if (numbers.size() == 5) {
                vertex.uv = {numbers[3], numbers[4]};
            } else {
                vertex.normal = {numbers[3], numbers[4], numbers[5]};
                vertex.uv = {numbers[6], numbers[7]};
            }
            if (numbers.size() == 12) {
                vertex.color = {numbers[8], numbers[9], numbers[10], numbers[11]};
            }
            desc.vertices.push_back(vertex);
        }
    }
    if (!render::mesh_desc_error(desc).empty()) {
        return std::nullopt;
    }
    return desc;
}

std::expected<render::ShaderDesc, std::string> parse_shader(std::string_view xml) {
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
            auto compiled = render::compile_slang_shader_stages(stage_vs, stage_fs, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        } else {
            const char* const slang_text = slang_child->GetText();
            if (slang_text == nullptr || trim(slang_text).empty()) {
                return std::unexpected("empty <slang> element in shader");
            }
            auto compiled = render::compile_slang_shader(trim(slang_text), vs_entry, fs_entry);
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
            auto compiled = render::compile_slang_shader_stages(stage_vs, stage_fs, vs_entry, fs_entry);
            if (!compiled) {
                return std::unexpected(std::move(compiled.error()));
            }
            desc = std::move(*compiled);
        } else {
            const char* const slang_text = root->GetText();
            if (slang_text == nullptr || trim(slang_text).empty()) {
                return std::unexpected("empty Slang shader source");
            }
            auto compiled = render::compile_slang_shader(trim(slang_text), vs_entry, fs_entry);
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

    // Optional `<glsl><vertex>...</vertex><fragment>...</fragment></glsl>` native backend override.
    if (glsl_child != nullptr) {
        std::string glsl_vs;
        std::string glsl_fs;
        if (!extract_vertex_fragment(glsl_child, glsl_vs, glsl_fs)) {
            return std::unexpected("<glsl> override requires non-empty <vertex> and <fragment>");
        }
        desc.vertex_src = std::move(glsl_vs);
        desc.fragment_src = std::move(glsl_fs);
        return desc;
    }

    if (has_slang) {
        return desc;
    }

    // Legacy `<xml><vertex>...</vertex><fragment>...</fragment></xml>` raw GLSL format.
    if (!extract_vertex_fragment(root, desc.vertex_src, desc.fragment_src)) {
        return std::unexpected("shader requires <slang>, lang=\"slang\", or <vertex> and <fragment>");
    }
    return desc;
}

std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml) {
    auto parsed = parse_shader(xml);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(*parsed);
}

} // namespace engine
