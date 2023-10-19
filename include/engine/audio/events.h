#pragma once

#include <engine/resources/asset_id.h>

namespace engine {

struct PlaySfxEvent {
    AssetId id;
    float volume_scale = 1.f;
};

struct PlayMusicEvent {
    AssetId id;
    bool loop = true;
    float fade_seconds = 0.f;
};

}
