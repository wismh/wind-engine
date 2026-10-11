#pragma once

// docs/tech/modules/Render.md

#include <engine/render/sprite.h>
#include <engine/resources/asset_id.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::render {

struct SpriteAnimationFrame {
    Sprite sprite;
    float duration = 0.1f;
};

struct SpriteAnimationClip {
    std::string name;
    float fps = 10.0f;
    bool loop = true;
    std::vector<SpriteAnimationFrame> frames;

    [[nodiscard]] float total_duration() const noexcept {
        float total = 0.0f;
        for (const auto& frame : frames) {
            total += frame.duration;
        }
        return total;
    }
};

struct SpriteAnimator {
    std::shared_ptr<SpriteAnimationClip> clip;
    float elapsed = 0.0f;
    std::size_t current_frame = 0;
    float speed = 1.0f;
    bool playing = true;
    // False until the animation system has put the current frame on the entity's Sprite (and again after `play` with
    // another clip and after `stop`), so a freshly spawned animator, even a paused one, shows its frame on the first
    // tick with no manual copy. Written by `run_sprite_animations`.
    bool frame_applied = false;

    void play(std::shared_ptr<SpriteAnimationClip> new_clip, bool restart = false) {
        if (clip != new_clip || restart) {
            clip = std::move(new_clip);
            elapsed = 0.0f;
            current_frame = 0;
            playing = true;
            frame_applied = false;
        }
    }

    void stop() {
        playing = false;
        elapsed = 0.0f;
        current_frame = 0;
        frame_applied = false;
    }

    void pause() {
        playing = false;
    }

    void resume() {
        playing = true;
    }
};

struct AnimationFrameDesc {
    AssetId texture;
    std::string sprite;
    std::optional<float> duration;
};

struct AnimationDesc {
    float fps = 10.0f;
    bool loop = true;
    std::vector<AnimationFrameDesc> frames;
};

[[nodiscard]] std::optional<AnimationDesc> parse_animation(std::string_view toml_text);

}
