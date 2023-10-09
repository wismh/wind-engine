#pragma once

#include <engine/resources/meta.h>

#include <glm/vec2.hpp>

#include <memory>

namespace engine {

// Opaque clip. Real MIX_Audio lives in src/ later; tests use this stub (or a null clip).
class Audio {
public:
    Audio() = default;
};

struct Sound {
    std::shared_ptr<Audio> clip;
    float volume = 1.f;
    glm::vec2 pitchRange{1.f, 1.f};
    bool loop = false;
    AudioBank bank = AudioBank::Sfx;
};

}
