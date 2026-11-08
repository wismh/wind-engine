#include "clip.h"

#include <utility>

namespace engine {

Audio::Audio()
    : impl_(std::make_unique<Impl>()) {}

Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;
Audio::~Audio() = default;

void audio_set_path(Audio& audio, std::string path) {
    AudioAccess::get(audio).path = std::move(path);
}

const std::string& audio_get_path(const Audio& audio) {
    return AudioAccess::get(audio).path;
}

} // namespace engine
