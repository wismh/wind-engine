#pragma once

// docs/tech/modules/Audio.md

#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

enum class AttenuationModel : std::uint8_t {
    Inverse,
    Linear,
    Exponential
};

struct SpatialAudioParams {
    glm::vec3 position{0.f, 0.f, 0.f};
    glm::vec3 velocity{0.f, 0.f, 0.f};
    glm::vec3 direction{0.f, 0.f, -1.f};
    float min_distance = 1.f;
    float max_distance = 100.f;
    float rolloff = 1.f;
    AttenuationModel attenuation = AttenuationModel::Inverse;
    float doppler_factor = 1.f;
};

} // namespace engine
