#include "importers.h"

#include "render/mesh_check.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

}

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
            // 5 numbers: position, uv (normal +Z). 8 numbers: position, normal, uv.
            std::vector<float> numbers;
            float number = 0.0f;
            while (values >> number) {
                numbers.push_back(number);
            }
            if (!values.eof() || (numbers.size() != 5 && numbers.size() != 8)) {
                return std::nullopt;
            }
            render::MeshVertex vertex;
            vertex.position = {numbers[0], numbers[1], numbers[2]};
            if (numbers.size() == 8) {
                vertex.normal = {numbers[3], numbers[4], numbers[5]};
            }
            vertex.uv = {numbers[numbers.size() - 2], numbers[numbers.size() - 1]};
            desc.vertices.push_back(vertex);
        }
    }
    if (!render::mesh_desc_error(desc).empty()) {
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
