#include <engine/audio/audio_system.h>

#include "fake_mixer.h"

#include <algorithm>
#include <utility>

namespace engine {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float pitch_ratio(const Sound& sound) {
    return sound.pitchRange.x;
}

} // namespace

struct AudioSystem::Impl {
    audio::FakeMixer mixer;
    float master = 1.f;
    float music_bus = 1.f;
    float sfx_bus = 1.f;

    [[nodiscard]] float sfx_gain(float voice_volume) const {
        return audio::final_gain(master, sfx_bus, voice_volume);
    }

    [[nodiscard]] float music_gain(float voice_volume) const {
        return audio::final_gain(master, music_bus, voice_volume);
    }
};

AudioSystem::AudioSystem()
    : impl_(std::make_unique<Impl>()) {}

AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;
AudioSystem::~AudioSystem() = default;

bool AudioSystem::Init() {
    impl_->mixer.reset();
    impl_->master = 1.f;
    impl_->music_bus = 1.f;
    impl_->sfx_bus = 1.f;
    return true;
}

void AudioSystem::Dispose() {
    impl_->mixer.reset();
}

void AudioSystem::Update(float dt) {
    if (dt < 0.f) {
        dt = 0.f;
    }
    impl_->mixer.tick(dt);
}

void AudioSystem::PlaySfx(const Sound& sound, float volume_scale) {
    audio::FakeTrack* const track = impl_->mixer.acquire_sfx();
    if (track == nullptr) {
        return;
    }
    const float voice = sound.volume * volume_scale;
    const float gain = impl_->sfx_gain(voice);
    track->start(gain, pitch_ratio(sound), sound.loop, voice);
    ++impl_->mixer.sfx_play_count;
    impl_->mixer.last_sfx_gain = gain;
}

void AudioSystem::PlayMusic(const Sound& sound, bool loop, float fade_seconds) {
    auto& mixer = impl_->mixer;
    const float target = impl_->music_gain(sound.volume);
    const float ratio = pitch_ratio(sound);

    if (!mixer.any_music_playing()) {
        mixer.active_music_index = 0;
        mixer.music[1].reset();
        mixer.music[0].start(target, ratio, loop, sound.volume);
        return;
    }

    const int outgoing = mixer.active_music_index;
    const int incoming = 1 - outgoing;
    mixer.active_music_index = incoming;

    if (fade_seconds <= 0.f) {
        mixer.music[static_cast<std::size_t>(outgoing)].reset();
        mixer.music[static_cast<std::size_t>(incoming)].start(target, ratio, loop, sound.volume);
        return;
    }

    mixer.music[static_cast<std::size_t>(incoming)].start(0.f, ratio, loop, sound.volume);
    mixer.music[static_cast<std::size_t>(incoming)].start_fade(target, fade_seconds, false);
    mixer.music[static_cast<std::size_t>(outgoing)].start_fade(0.f, fade_seconds, true);
}

void AudioSystem::StopMusic(float fade_seconds) {
    for (audio::FakeTrack& track : impl_->mixer.music) {
        if (!track.playing) {
            continue;
        }
        track.start_fade(0.f, fade_seconds, true);
    }
}

bool AudioSystem::IsMusicPlaying() const {
    return impl_->mixer.any_music_playing();
}

LoopingSfxHandle AudioSystem::CreateLoopingSfx() {
    auto& mixer = impl_->mixer;
    if (mixer.next_looping_id == 0) {
        mixer.next_looping_id = 1;
    }
    const LoopingSfxHandle handle{mixer.next_looping_id};
    ++mixer.next_looping_id;
    if (mixer.next_looping_id == 0) {
        mixer.next_looping_id = 1;
    }
    mixer.looping.emplace(handle.id, audio::FakeTrack{});
    return handle;
}

void AudioSystem::PlayLoopingSfx(LoopingSfxHandle handle, const Sound& sound, float fade_in) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    const float voice = sound.volume;
    const float target = impl_->sfx_gain(voice);
    if (fade_in <= 0.f) {
        track->start(target, pitch_ratio(sound), true, voice);
        return;
    }
    track->start(0.f, pitch_ratio(sound), true, voice);
    track->start_fade(target, fade_in, false);
}

void AudioSystem::StopLoopingSfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    track->start_fade(0.f, fade_out, true);
}

void AudioSystem::ReleaseLoopingSfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    if (fade_out <= 0.f) {
        impl_->mixer.looping.erase(handle.id);
        return;
    }
    track->release_when_fade_done = true;
    track->start_fade(0.f, fade_out, true);
}

void AudioSystem::SetMasterVolume(float volume) {
    impl_->master = clamp01(volume);
}

void AudioSystem::SetMusicVolume(float volume) {
    impl_->music_bus = clamp01(volume);
}

void AudioSystem::SetSfxVolume(float volume) {
    impl_->sfx_bus = clamp01(volume);
}

int AudioSystem::sfx_pool_size() const {
    return audio::kSfxPoolSize;
}

int AudioSystem::sfx_playing_count() const {
    return impl_->mixer.sfx_playing_count();
}

int AudioSystem::sfx_play_count() const {
    return impl_->mixer.sfx_play_count;
}

float AudioSystem::last_sfx_gain() const {
    return impl_->mixer.last_sfx_gain;
}

int AudioSystem::active_music_index() const {
    return impl_->mixer.active_music_index;
}

bool AudioSystem::music_slot_playing(int slot) const {
    if (slot < 0 || slot >= audio::kMusicSlotCount) {
        return false;
    }
    return impl_->mixer.music[static_cast<std::size_t>(slot)].playing;
}

float AudioSystem::music_slot_gain(int slot) const {
    if (slot < 0 || slot >= audio::kMusicSlotCount) {
        return 0.f;
    }
    return impl_->mixer.music[static_cast<std::size_t>(slot)].gain;
}

int AudioSystem::looping_track_count() const {
    return static_cast<int>(impl_->mixer.looping.size());
}

}
