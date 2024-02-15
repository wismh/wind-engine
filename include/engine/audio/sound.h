#pragma once

#include <engine/resources/meta.h>

#include <glm/vec2.hpp>

#include <memory>

namespace engine {

struct AudioAccess;

// Opaque clip. MIX_Audio stays in src/; public headers never include SDL_mixer.
class Audio {
public:
    Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) noexcept;
    Audio& operator=(Audio&&) noexcept;
    ~Audio();

private:
    friend struct AudioAccess;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Sound {
    std::shared_ptr<Audio> clip;
    float volume = 1.f;
    glm::vec2 pitch_range{1.f, 1.f};
    bool loop = false;
    AudioBank bank = AudioBank::Sfx;
};

}
