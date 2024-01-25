#include "importers.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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

}

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
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        if (!(values >> x >> y >> z >> u >> v)) {
            return std::nullopt;
        }
        std::string extra;
        if (values >> extra) {
            return std::nullopt;
        }
        render::MeshVertex vertex;
        vertex.position = {x, y, z};
        vertex.uv = {u, v};
        desc.vertices.push_back(vertex);
    }
    if (desc.vertices.empty()) {
        return std::nullopt;
    }
    return desc;
}

std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return std::nullopt;
    }
    const tinyxml2::XMLElement* const root = doc.RootElement();
    if (root == nullptr) {
        return std::nullopt;
    }
    const tinyxml2::XMLElement* const vertex = root->FirstChildElement("vertex");
    const tinyxml2::XMLElement* const fragment = root->FirstChildElement("fragment");
    if (vertex == nullptr || fragment == nullptr) {
        return std::nullopt;
    }
    const char* const vs = vertex->GetText();
    const char* const fs = fragment->GetText();
    if (vs == nullptr || fs == nullptr) {
        return std::nullopt;
    }
    render::ShaderDesc desc;
    desc.vertex_src = std::string(trim(vs));
    desc.fragment_src = std::string(trim(fs));
    if (desc.vertex_src.empty() || desc.fragment_src.empty()) {
        return std::nullopt;
    }
    return desc;
}

}
