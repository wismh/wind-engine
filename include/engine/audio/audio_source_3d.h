#pragma once

// docs/tech/modules/Audio.md

#include <engine/audio/audio_system.h>
#include <engine/audio/spatial.h>
#include <engine/resources/asset_id.h>

namespace engine {

struct AudioSource3D {
    AssetId sound;
    SpatialAudioParams params{};
    float volume = 1.f;
    bool loop = false;
    bool playing = true;
    LoopingSfxHandle handle{};
};

} // namespace engine
