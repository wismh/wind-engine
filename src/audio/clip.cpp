#include "clip.h"

#ifdef ENGINE_WITH_AUDIO
#include <SDL3_mixer/SDL_mixer.h>
#endif

#include <utility>

namespace engine {

Audio::Audio()
    : impl_(std::make_unique<Impl>()) {}

Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;

Audio::~Audio() {
#ifdef ENGINE_WITH_AUDIO
    if (impl_ && impl_->mix != nullptr) {
        // MIX_Quit in AudioSystem::dispose frees remaining MIX_Audio. Skip
        // MIX_DestroyAudio here so a clip can outlive mixer teardown.
        impl_->mix = nullptr;
    }
#endif
}

void audio_set_path(Audio& audio, std::string path) {
    AudioAccess::get(audio).path = std::move(path);
}

#ifdef ENGINE_WITH_AUDIO
MIX_Audio* audio_decoded(Audio& audio, MIX_Mixer* mixer) {
    auto& impl = AudioAccess::get(audio);
    if (impl.mix != nullptr) {
        return impl.mix;
    }
    if (mixer == nullptr || impl.path.empty()) {
        return nullptr;
    }
    impl.mix = MIX_LoadAudio(mixer, impl.path.c_str(), false);
    return impl.mix;
}
#endif

}
