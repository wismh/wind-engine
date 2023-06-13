#include <engine/audio/audio_system.h>

#include "clip.h"
#include "fake_mixer.h"

#ifdef ENGINE_WITH_AUDIO
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace engine {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float pitch_ratio(const Sound& sound) {
    return sound.pitchRange.x;
}

#ifdef ENGINE_WITH_AUDIO

void stop_mix_track(MIX_Track* track) {
    if (track != nullptr && MIX_TrackPlaying(track)) {
        MIX_StopTrack(track, 0);
    }
}

void play_mix_track(MIX_Track* track, MIX_Audio* clip, float gain, float ratio, bool loop) {
    if (track == nullptr || clip == nullptr) {
        return;
    }
    MIX_SetTrackAudio(track, clip);
    MIX_SetTrackGain(track, gain);
    MIX_SetTrackFrequencyRatio(track, ratio);
    if (loop) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        if (props == 0) {
            MIX_PlayTrack(track, 0);
            return;
        }
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);
        return;
    }
    MIX_PlayTrack(track, 0);
}

void apply_mix_track(MIX_Track* track, const audio::FakeTrack& fake) {
    if (track == nullptr) {
        return;
    }
    if (!fake.playing) {
        stop_mix_track(track);
        return;
    }
    MIX_SetTrackGain(track, fake.gain);
    MIX_SetTrackFrequencyRatio(track, fake.frequency_ratio);
}

MIX_Audio* clip_for(const Sound& sound, MIX_Mixer* mixer) {
    if (sound.clip == nullptr || mixer == nullptr) {
        return nullptr;
    }
    return audio_decoded(*sound.clip, mixer);
}

#endif

} // namespace

struct AudioSystem::Impl {
    audio::FakeMixer mixer;
    float master = 1.f;
    float music_bus = 1.f;
    float sfx_bus = 1.f;
#ifdef ENGINE_WITH_AUDIO
    MIX_Mixer* device = nullptr;
    std::array<MIX_Track*, audio::kSfxPoolSize> sfx_tracks{};
    std::array<MIX_Track*, audio::kMusicSlotCount> music_tracks{};
    std::unordered_map<std::uint32_t, MIX_Track*> looping_tracks;
    bool sdl_audio = false;
    bool mix_inited = false;
#endif

    [[nodiscard]] float sfx_gain(float voice_volume) const {
        return audio::final_gain(master, sfx_bus, voice_volume);
    }

    [[nodiscard]] float music_gain(float voice_volume) const {
        return audio::final_gain(master, music_bus, voice_volume);
    }

#ifdef ENGINE_WITH_AUDIO
    void destroy_mix() {
        for (auto& [id, track] : looping_tracks) {
            if (track != nullptr) {
                MIX_DestroyTrack(track);
            }
        }
        looping_tracks.clear();
        for (MIX_Track*& track : sfx_tracks) {
            if (track != nullptr) {
                MIX_DestroyTrack(track);
                track = nullptr;
            }
        }
        for (MIX_Track*& track : music_tracks) {
            if (track != nullptr) {
                MIX_DestroyTrack(track);
                track = nullptr;
            }
        }
        if (device != nullptr) {
            MIX_DestroyMixer(device);
            device = nullptr;
        }
        if (mix_inited) {
            MIX_Quit();
            mix_inited = false;
        }
        if (sdl_audio) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdl_audio = false;
        }
    }

    [[nodiscard]] bool create_mix() {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            return false;
        }
        sdl_audio = true;
        if (!MIX_Init()) {
            return false;
        }
        mix_inited = true;
        device = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (device == nullptr) {
            return false;
        }
        for (MIX_Track*& track : sfx_tracks) {
            track = MIX_CreateTrack(device);
            if (track == nullptr) {
                return false;
            }
        }
        for (MIX_Track*& track : music_tracks) {
            track = MIX_CreateTrack(device);
            if (track == nullptr) {
                return false;
            }
        }
        return true;
    }

    ~Impl() {
        destroy_mix();
    }

    void sync_ended_sfx() {
        if (device == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < mixer.sfx.size(); ++i) {
            audio::FakeTrack& fake = mixer.sfx[i];
            MIX_Track* const track = sfx_tracks[i];
            if (fake.playing && !fake.loop && track != nullptr && !MIX_TrackPlaying(track)) {
                fake.reset();
            }
        }
    }

    void prune_looping_mix() {
        for (auto it = looping_tracks.begin(); it != looping_tracks.end();) {
            if (mixer.looping.find(it->first) == mixer.looping.end()) {
                if (it->second != nullptr) {
                    MIX_DestroyTrack(it->second);
                }
                it = looping_tracks.erase(it);
            } else {
                ++it;
            }
        }
    }

    void apply_mix() {
        if (device == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < mixer.sfx.size(); ++i) {
            apply_mix_track(sfx_tracks[i], mixer.sfx[i]);
        }
        for (std::size_t i = 0; i < mixer.music.size(); ++i) {
            apply_mix_track(music_tracks[i], mixer.music[i]);
        }
        for (auto& [id, fake] : mixer.looping) {
            const auto it = looping_tracks.find(id);
            apply_mix_track(it == looping_tracks.end() ? nullptr : it->second, fake);
        }
    }

    void play_sfx_mix(std::size_t index, const Sound& sound, float gain, float ratio) {
        play_mix_track(sfx_tracks[index], clip_for(sound, device), gain, ratio, sound.loop);
    }

    void play_music_mix(int slot, const Sound& sound, float gain, float ratio, bool loop) {
        play_mix_track(music_tracks[static_cast<std::size_t>(slot)], clip_for(sound, device), gain, ratio, loop);
    }

    void play_looping_mix(std::uint32_t id, const Sound& sound, float gain, float ratio) {
        const auto it = looping_tracks.find(id);
        if (it == looping_tracks.end()) {
            return;
        }
        play_mix_track(it->second, clip_for(sound, device), gain, ratio, true);
    }
#endif
};

AudioSystem::AudioSystem()
    : impl_(std::make_unique<Impl>()) {}

AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;

AudioSystem::~AudioSystem() {
    Dispose();
}

bool AudioSystem::Init() {
    Dispose();
    impl_->mixer.reset();
    impl_->master = 1.f;
    impl_->music_bus = 1.f;
    impl_->sfx_bus = 1.f;
#ifdef ENGINE_WITH_AUDIO
    if (!impl_->create_mix()) {
        impl_->destroy_mix();
        return false;
    }
#endif
    return true;
}

void AudioSystem::Dispose() {
    if (!impl_) {
        return;
    }
    impl_->mixer.reset();
#ifdef ENGINE_WITH_AUDIO
    impl_->destroy_mix();
#endif
}

void AudioSystem::Update(float dt) {
    if (dt < 0.f) {
        dt = 0.f;
    }
#ifdef ENGINE_WITH_AUDIO
    impl_->sync_ended_sfx();
#endif
    impl_->mixer.tick(dt);
#ifdef ENGINE_WITH_AUDIO
    impl_->prune_looping_mix();
    impl_->apply_mix();
#endif
}

void AudioSystem::PlaySfx(const Sound& sound, float volume_scale) {
#ifdef ENGINE_WITH_AUDIO
    impl_->sync_ended_sfx();
#endif
    audio::FakeTrack* const track = impl_->mixer.acquire_sfx();
    if (track == nullptr) {
        return;
    }
    const float voice = sound.volume * volume_scale;
    const float gain = impl_->sfx_gain(voice);
    const float ratio = pitch_ratio(sound);
    track->start(gain, ratio, sound.loop, voice);
    ++impl_->mixer.sfx_play_count;
    impl_->mixer.last_sfx_gain = gain;
#ifdef ENGINE_WITH_AUDIO
    const std::size_t index = static_cast<std::size_t>(track - impl_->mixer.sfx.data());
    impl_->play_sfx_mix(index, sound, gain, ratio);
#endif
}

void AudioSystem::PlayMusic(const Sound& sound, bool loop, float fade_seconds) {
    auto& mixer = impl_->mixer;
    const float target = impl_->music_gain(sound.volume);
    const float ratio = pitch_ratio(sound);

    if (!mixer.any_music_playing()) {
        mixer.active_music_index = 0;
        mixer.music[1].reset();
        mixer.music[0].start(target, ratio, loop, sound.volume);
#ifdef ENGINE_WITH_AUDIO
        stop_mix_track(impl_->music_tracks[1]);
        impl_->play_music_mix(0, sound, target, ratio, loop);
#endif
        return;
    }

    const int outgoing = mixer.active_music_index;
    const int incoming = 1 - outgoing;
    mixer.active_music_index = incoming;

    if (fade_seconds <= 0.f) {
        mixer.music[static_cast<std::size_t>(outgoing)].reset();
        mixer.music[static_cast<std::size_t>(incoming)].start(target, ratio, loop, sound.volume);
#ifdef ENGINE_WITH_AUDIO
        stop_mix_track(impl_->music_tracks[static_cast<std::size_t>(outgoing)]);
        impl_->play_music_mix(incoming, sound, target, ratio, loop);
#endif
        return;
    }

    mixer.music[static_cast<std::size_t>(incoming)].start(0.f, ratio, loop, sound.volume);
    mixer.music[static_cast<std::size_t>(incoming)].start_fade(target, fade_seconds, false);
    mixer.music[static_cast<std::size_t>(outgoing)].start_fade(0.f, fade_seconds, true);
#ifdef ENGINE_WITH_AUDIO
    impl_->play_music_mix(incoming, sound, 0.f, ratio, loop);
    impl_->apply_mix();
#endif
}

void AudioSystem::StopMusic(float fade_seconds) {
    for (audio::FakeTrack& track : impl_->mixer.music) {
        if (!track.playing) {
            continue;
        }
        track.start_fade(0.f, fade_seconds, true);
    }
#ifdef ENGINE_WITH_AUDIO
    impl_->apply_mix();
#endif
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
#ifdef ENGINE_WITH_AUDIO
    if (impl_->device != nullptr) {
        MIX_Track* const track = MIX_CreateTrack(impl_->device);
        if (track != nullptr) {
            impl_->looping_tracks.emplace(handle.id, track);
        }
    }
#endif
    return handle;
}

void AudioSystem::PlayLoopingSfx(LoopingSfxHandle handle, const Sound& sound, float fade_in) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    const float voice = sound.volume;
    const float target = impl_->sfx_gain(voice);
    const float ratio = pitch_ratio(sound);
    if (fade_in <= 0.f) {
        track->start(target, ratio, true, voice);
    } else {
        track->start(0.f, ratio, true, voice);
        track->start_fade(target, fade_in, false);
    }
#ifdef ENGINE_WITH_AUDIO
    impl_->play_looping_mix(handle.id, sound, track->gain, ratio);
#endif
}

void AudioSystem::StopLoopingSfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    track->start_fade(0.f, fade_out, true);
#ifdef ENGINE_WITH_AUDIO
    impl_->apply_mix();
#endif
}

void AudioSystem::ReleaseLoopingSfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    if (fade_out <= 0.f) {
        impl_->mixer.looping.erase(handle.id);
#ifdef ENGINE_WITH_AUDIO
        impl_->prune_looping_mix();
#endif
        return;
    }
    track->release_when_fade_done = true;
    track->start_fade(0.f, fade_out, true);
#ifdef ENGINE_WITH_AUDIO
    impl_->apply_mix();
#endif
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
