#pragma once

#include <engine/audio/sound.h>

#include <string>

#ifdef ENGINE_WITH_AUDIO
struct MIX_Audio;
struct MIX_Mixer;
#endif

namespace engine {

struct Audio::Impl {
    std::string path;
#ifdef ENGINE_WITH_AUDIO
    MIX_Audio* mix = nullptr;
#endif
};

struct AudioAccess {
    static Audio::Impl& get(Audio& audio) {
        return *audio.impl_;
    }
};

void audio_set_path(Audio& audio, std::string path);

#ifdef ENGINE_WITH_AUDIO
[[nodiscard]] MIX_Audio* audio_decoded(Audio& audio, MIX_Mixer* mixer);
#endif

}
