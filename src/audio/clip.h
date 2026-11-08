#pragma once

#include <engine/audio/sound.h>

#include <string>

namespace engine {

struct AudioAccess {
    static Audio::Impl& get(Audio& audio) {
        return *audio.impl_;
    }
    static const Audio::Impl& get(const Audio& audio) {
        return *audio.impl_;
    }
};

struct Audio::Impl {
    std::string path;
};

void audio_set_path(Audio& audio, std::string path);
[[nodiscard]] const std::string& audio_get_path(const Audio& audio);

} // namespace engine
