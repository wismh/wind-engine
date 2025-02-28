#include <engine/render/animation.h>

#include <toml++/toml.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace engine::render {
namespace {

std::optional<float> node_as_float(const toml::node& node) {
    if (const auto f = node.value<double>()) {
        return static_cast<float>(*f);
    }
    if (const auto i = node.value<std::int64_t>()) {
        return static_cast<float>(*i);
    }
    return std::nullopt;
}

}

std::optional<AnimationDesc> parse_animation(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        AnimationDesc desc;

        if (const toml::node* fps_node = table.get("fps")) {
            const auto fps = node_as_float(*fps_node);
            if (!fps || *fps <= 0.0f) {
                return std::nullopt;
            }
            desc.fps = *fps;
        }

        if (const auto loop = table["loop"].value<bool>()) {
            desc.loop = *loop;
        }

        const toml::node* frames_node = table.get("frames");
        if (frames_node == nullptr) {
            return std::nullopt;
        }
        const toml::array* const arr = frames_node->as_array();
        if (arr == nullptr) {
            return std::nullopt;
        }

        for (const toml::node& frame_elem : *arr) {
            const toml::table* const frame_tab = frame_elem.as_table();
            if (frame_tab == nullptr) {
                return std::nullopt;
            }

            const auto tex_str = (*frame_tab)["texture"].value<std::string>();
            if (!tex_str) {
                return std::nullopt;
            }
            const auto tex_id = AssetId::parse(*tex_str);
            if (!tex_id) {
                return std::nullopt;
            }

            AnimationFrameDesc frame_desc;
            frame_desc.texture = *tex_id;

            if (const auto sprite = (*frame_tab)["sprite"].value<std::string>()) {
                frame_desc.sprite = *sprite;
            }

            if (const toml::node* dur_node = frame_tab->get("duration")) {
                const auto dur = node_as_float(*dur_node);
                if (!dur || *dur <= 0.0f) {
                    return std::nullopt;
                }
                frame_desc.duration = *dur;
            }

            desc.frames.push_back(std::move(frame_desc));
        }

        return desc;
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

}
