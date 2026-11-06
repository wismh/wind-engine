#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphics.h>
#include <engine/resources/asset_id.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace engine {
class AssetsDb;
class IFatalError;
}

namespace engine::render {

class CommandBuffer;

// One fullscreen pass. `shader` samples `uScene`. `params` is `uParams`. `sample_depth` asks the backend for a
// depth texture (`uDepth`). The builtin depth shader forces that flag on when the stack is resolved.
struct PostEffect {
    AssetId shader{};
    glm::vec4 params{1.0f, 1.0f, 1.0f, 1.0f};
    bool sample_depth = false;

    bool operator==(const PostEffect&) const = default;
};

// On the ActiveCamera entity. No component, or `hdr == false` with an empty list, draws straight to the window.
// `hdr` stores the world in a float target and tonemaps after `effects`. Bloom (`builtin::shader_post_bloom`) is
// fatal when `hdr` is false.
struct PostStack {
    bool hdr = false;
    std::vector<PostEffect> effects;

    [[nodiscard]] bool active() const noexcept { return hdr || !effects.empty(); }

    bool operator==(const PostStack&) const = default;
};

// One resolved pass. `bloom` is the downsample chain, not a single sample of `uScene`.
struct PostPass {
    std::shared_ptr<IShader> shader;
    glm::vec4 params{1.0f, 1.0f, 1.0f, 1.0f};
    bool sample_depth = false;
    bool bloom = false;
};

// What `run_render` copies onto `CmdSetView`. `tonemap` and `ldr_copy` are set only when `hdr` is true.
// `ldr_copy` is the grade shader. The RGBA8 fallback draws it at white when the effect list is empty, so the scene
// still reaches the window. Tonemap is not that copy: Reinhard would darken an image already in 0..1.
struct PostChain {
    bool hdr = false;
    bool sample_depth = false;
    std::vector<PostPass> effects;
    std::shared_ptr<IShader> tonemap;
    std::shared_ptr<IShader> ldr_copy;

    [[nodiscard]] bool active() const noexcept { return hdr || !effects.empty() || sample_depth; }

    [[nodiscard]] bool samples_depth() const noexcept {
        if (sample_depth) {
            return true;
        }
        for (const PostPass& pass : effects) {
            if (pass.sample_depth) {
                return true;
            }
        }
        return false;
    }
};

// Checked before any shader is loaded. An inactive stack is an empty value, not an error.
struct PreparedPost {
    bool hdr = false;
    std::vector<PostEffect> effects;
};

[[nodiscard]] std::expected<PreparedPost, std::string> prepare_post_stack(const PostStack* stack);

// Loads every shader in `prepared`, including the tonemap when `hdr` is set. A missing asset is fatal.
[[nodiscard]] PostChain resolve_post_chain(const PreparedPost& prepared, AssetsDb* assets, IFatalError* fatal);

enum class SceneColorFormat : std::uint8_t {
    Rgba8,
    R11G11B10,
    Rgba16F,
};

// `float_supported` is false when the backend cannot render to the float format this window needs.
[[nodiscard]] constexpr SceneColorFormat scene_color_format(bool hdr, bool transparent, bool float_supported) noexcept {
    if (!hdr || !float_supported) {
        return SceneColorFormat::Rgba8;
    }
    return transparent ? SceneColorFormat::Rgba16F : SceneColorFormat::R11G11B10;
}

// Where one post pass reads and writes. The world always writes SceneA first.
enum class PostImage : std::uint8_t {
    SceneA,
    SceneB,
    Window,
};

struct PostStep {
    bool tonemap = false;
    // Scene color copied as stored. Set for an HDR stack whose float target was unavailable and whose effect list is
    // empty, so the window still shows the scene.
    bool copy = false;
    int effect = -1;
    PostImage source = PostImage::SceneA;
    PostImage dest = PostImage::Window;
};

// `hdr` is the effective flag (false when the float format was unavailable). The last user pass writes the window
// when `hdr` is false; otherwise it stays on a scene image and the tonemap writes the window. `copy_scene` adds one
// SceneA-to-window copy when `hdr` is false and `effect_count` is 0.
[[nodiscard]] std::vector<PostStep> post_steps(bool hdr, int effect_count, bool copy_scene = false);

enum class FrameTarget : std::uint8_t {
    Window,
    Scene,
};

// One entry per command. World draws that follow a full-window view with an active chain are Scene until
// `CmdPostProcess`. A view with a viewport stays on the window. `CmdDrawUI` after the marker is Window.
[[nodiscard]] std::vector<FrameTarget> frame_targets(const CommandBuffer& commands);

} // namespace engine::render
