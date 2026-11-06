#include "importers.h"

#include "render/mesh_check.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

std::expected<render::TextureArrayDesc, std::string> texture_array_from_frames(
        const render::TextureDesc& image, std::span<const SpriteMeta> frames) {
    if (frames.empty()) {
        return std::unexpected("the texture array has no frames");
    }
    if (frames.size() > static_cast<std::size_t>(render::kMaxTextureArrayLayers)) {
        return std::unexpected(std::format(
                "the texture array has {} frames, more than {}", frames.size(), render::kMaxTextureArrayLayers));
    }
    const int width = frames.front().rect.w;
    const int height = frames.front().rect.h;
    const std::size_t image_bytes =
            static_cast<std::size_t>(std::max(image.width, 0)) * static_cast<std::size_t>(std::max(image.height, 0)) * 4u;
    if (image.rgba.size() < image_bytes) {
        return std::unexpected("the texture array's image has fewer pixels than its size");
    }
    render::TextureArrayDesc desc;
    desc.width = width;
    desc.height = height;
    desc.layers = static_cast<int>(frames.size());
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    desc.rgba.resize(row_bytes * static_cast<std::size_t>(height) * frames.size());
    std::size_t out = 0;
    for (const SpriteMeta& frame : frames) {
        const SpriteRect& rect = frame.rect;
        if (rect.w != width || rect.h != height) {
            return std::unexpected(std::format("frame \"{}\" is {} x {}, not {} x {} like the first frame",
                    frame.name, rect.w, rect.h, width, height));
        }
        if (rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0 || rect.x + rect.w > image.width ||
                rect.y + rect.h > image.height) {
            return std::unexpected(std::format("frame \"{}\" ({}, {}, {} x {}) is not inside the {} x {} image",
                    frame.name, rect.x, rect.y, rect.w, rect.h, image.width, image.height));
        }
        for (int y = 0; y < height; ++y) {
            const std::size_t src = (static_cast<std::size_t>(rect.y + y) * static_cast<std::size_t>(image.width) +
                                            static_cast<std::size_t>(rect.x)) * 4u;
            std::memcpy(desc.rgba.data() + out, image.rgba.data() + src, row_bytes);
            out += row_bytes;
        }
    }
    return desc;
}

} // namespace engine
