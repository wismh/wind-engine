#include "cli/screenshot.h"

#include "cli/json.h"
#include "resources/importers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace engine::cli {
    namespace {

        std::filesystem::path utf8_path(const std::string &text) {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
        }

        bool write_bytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }
            file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(file);
        }

    } // namespace

    render::Rect snap_to_pixels(const render::Rect &rect, int width, int height) {
        const float left = std::clamp(std::floor(rect.x), 0.0f, static_cast<float>(width));
        const float top = std::clamp(std::floor(rect.y), 0.0f, static_cast<float>(height));
        const float right = std::clamp(std::ceil(rect.x + rect.w), 0.0f, static_cast<float>(width));
        const float bottom = std::clamp(std::ceil(rect.y + rect.h), 0.0f, static_cast<float>(height));
        if (!(right > left) || !(bottom > top)) {
            return render::Rect{left, top, 0.0f, 0.0f};
        }
        return render::Rect{left, top, right - left, bottom - top};
    }

    render::TextureDesc crop_image(const render::TextureDesc &image, const render::Rect &box) {
        render::TextureDesc out;
        out.width = static_cast<int>(box.w);
        out.height = static_cast<int>(box.h);
        const std::size_t source_row = static_cast<std::size_t>(image.width) * 4u;
        const std::size_t row = static_cast<std::size_t>(out.width) * 4u;
        out.rgba.resize(row * static_cast<std::size_t>(out.height));
        const std::size_t left = static_cast<std::size_t>(box.x) * 4u;
        for (int y = 0; y < out.height; ++y) {
            const std::size_t source_y = static_cast<std::size_t>(box.y) + static_cast<std::size_t>(y);
            std::copy_n(image.rgba.data() + source_y * source_row + left, row,
                        out.rgba.data() + static_cast<std::size_t>(y) * row);
        }
        return out;
    }

    std::optional<std::string> screenshot_request_error(const CliRequest &request) {
        if (request.path.empty() || !utf8_path(request.path).is_absolute()) {
            return error_json("screenshot needs an absolute path");
        }
        return std::nullopt;
    }

    std::string screenshot_json(ecs::World *world, const CliRequest &request, const render::TextureDesc &image) {
        render::Rect box{0.0f, 0.0f, static_cast<float>(image.width), static_cast<float>(image.height)};
        if (!request.selector.empty()) {
            if (world == nullptr) {
                return error_json(std::format("no world on window {}", request.window));
            }
            const std::expected<render::Rect, std::string> element = element_window_rect(*world, request);
            if (!element) {
                return element.error();
            }
            box = snap_to_pixels(*element, image.width, image.height);
            if (box.w <= 0.0f || box.h <= 0.0f) {
                return error_json("element is outside the window");
            }
        }
        const std::vector<std::uint8_t> png =
                encode_png_rgba(request.selector.empty() ? image : crop_image(image, box));
        if (png.empty()) {
            return error_json("could not encode the png");
        }
        if (!write_bytes(utf8_path(request.path), png)) {
            return error_json(std::format("could not write {}", request.path));
        }

        Json json;
        json.begin_object();
        json.key("ok");
        json.boolean(true);
        json.key("result");
        json.begin_object();
        json.key("path");
        json.string(request.path);
        json.key("window");
        json.integer(static_cast<std::int64_t>(request.window));
        json.key("width");
        json.integer(static_cast<std::int64_t>(box.w));
        json.key("height");
        json.integer(static_cast<std::int64_t>(box.h));
        json.key("rect");
        json.begin_object();
        json.key("x");
        json.integer(static_cast<std::int64_t>(box.x));
        json.key("y");
        json.integer(static_cast<std::int64_t>(box.y));
        json.key("w");
        json.integer(static_cast<std::int64_t>(box.w));
        json.key("h");
        json.integer(static_cast<std::int64_t>(box.h));
        json.end_object();
        json.end_object();
        json.end_object();
        return json.str();
    }

} // namespace engine::cli
