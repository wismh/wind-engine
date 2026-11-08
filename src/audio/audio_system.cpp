#include <engine/audio/audio_system.h>

#include "clip.h"
#include "fake_mixer.h"

#ifdef ENGINE_WITH_AUDIO
#include <miniaudio.h>
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
    return sound.pitch_range.x;
}

#ifdef ENGINE_WITH_AUDIO

ma_attenuation_model to_ma_attenuation(AttenuationModel model) {
    switch (model) {
    case AttenuationModel::Inverse:
        return ma_attenuation_model_inverse;
    case AttenuationModel::Linear:
        return ma_attenuation_model_linear;
    case AttenuationModel::Exponential:
        return ma_attenuation_model_exponential;
    }
    return ma_attenuation_model_inverse;
}

#endif

} // namespace

struct AudioSystem::Impl {
    audio::FakeMixer mixer;
    float master = 1.f;
    float music_bus = 1.f;
    float sfx_bus = 1.f;

#ifdef ENGINE_WITH_AUDIO
    ma_engine engine{};
    ma_sound_group music_group{};
    ma_sound_group sfx_group{};
    bool engine_inited = false;

    struct SfxVoice {
        ma_sound sound{};
        bool active = false;
    };
    std::array<SfxVoice, audio::kSfxPoolSize> sfx_voices{};

    struct MusicSlot {
        ma_sound sound{};
        bool active = false;
        std::string path;
    };
    std::array<MusicSlot, audio::kMusicSlotCount> music_slots{};

    struct LoopingVoice {
        ma_sound sound{};
        bool active = false;
        std::string path;
    };
    std::unordered_map<std::uint32_t, LoopingVoice> looping_voices;
#endif

    [[nodiscard]] float sfx_gain(float voice_volume) const {
        return audio::final_gain(master, sfx_bus, voice_volume);
    }

    [[nodiscard]] float music_gain(float voice_volume) const {
        return audio::final_gain(master, music_bus, voice_volume);
    }

#ifdef ENGINE_WITH_AUDIO
    bool create_engine() {
        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &engine);
        if (result != MA_SUCCESS) {
            return false;
        }
        result = ma_sound_group_init(&engine, 0, nullptr, &music_group);
        if (result != MA_SUCCESS) {
            ma_engine_uninit(&engine);
            return false;
        }
        result = ma_sound_group_init(&engine, 0, nullptr, &sfx_group);
        if (result != MA_SUCCESS) {
            ma_sound_group_uninit(&music_group);
            ma_engine_uninit(&engine);
            return false;
        }
        engine_inited = true;
        ma_engine_set_volume(&engine, master);
        ma_sound_group_set_volume(&music_group, music_bus);
        ma_sound_group_set_volume(&sfx_group, sfx_bus);
        return true;
    }

    void destroy_engine() {
        if (!engine_inited) {
            return;
        }
        for (auto& [id, voice] : looping_voices) {
            if (voice.active) {
                ma_sound_uninit(&voice.sound);
                voice.active = false;
            }
        }
        looping_voices.clear();

        for (auto& voice : sfx_voices) {
            if (voice.active) {
                ma_sound_uninit(&voice.sound);
                voice.active = false;
            }
        }
        for (auto& slot : music_slots) {
            if (slot.active) {
                ma_sound_uninit(&slot.sound);
                slot.active = false;
            }
        }
        ma_sound_group_uninit(&music_group);
        ma_sound_group_uninit(&sfx_group);
        ma_engine_uninit(&engine);
        engine_inited = false;
    }
#endif

    ~Impl() {
#ifdef ENGINE_WITH_AUDIO
        destroy_engine();
#endif
    }

    void update_music_gains() {
        for (audio::FakeTrack& track : mixer.music) {
            if (!track.playing) {
                continue;
            }
            const float target = music_gain(track.voice_volume);
            if (track.fading) {
                if (!track.stop_when_fade_done) {
                    track.fade_to = target;
                }
            } else {
                track.gain = target;
            }
        }
    }

    void update_sfx_gains() {
        for (audio::FakeTrack& track : mixer.sfx) {
            if (!track.playing) {
                continue;
            }
            if (!track.fading) {
                track.gain = sfx_gain(track.voice_volume);
            }
        }
        for (auto& [id, track] : mixer.looping) {
            if (!track.playing) {
                continue;
            }
            const float target = sfx_gain(track.voice_volume);
            if (track.fading) {
                if (!track.stop_when_fade_done) {
                    track.fade_to = target;
                }
            } else {
                track.gain = target;
            }
        }
    }
};

AudioSystem::AudioSystem()
    : impl_(std::make_unique<Impl>()) {}

AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;

AudioSystem::~AudioSystem() {
    dispose();
}

bool AudioSystem::init() {
    dispose();
    impl_->mixer.reset();
    impl_->master = 1.f;
    impl_->music_bus = 1.f;
    impl_->sfx_bus = 1.f;
#ifdef ENGINE_WITH_AUDIO
    if (!impl_->create_engine()) {
        impl_->destroy_engine();
        return false;
    }
#endif
    return true;
}

void AudioSystem::dispose() {
    if (!impl_) {
        return;
    }
    impl_->mixer.reset();
#ifdef ENGINE_WITH_AUDIO
    impl_->destroy_engine();
#endif
}

void AudioSystem::update(float dt) {
    if (dt < 0.f) {
        dt = 0.f;
    }
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        for (std::size_t i = 0; i < impl_->sfx_voices.size(); ++i) {
            auto& voice = impl_->sfx_voices[i];
            audio::FakeTrack& fake = impl_->mixer.sfx[i];
            if (voice.active && (!ma_sound_is_playing(&voice.sound) || ma_sound_at_end(&voice.sound))) {
                ma_sound_uninit(&voice.sound);
                voice.active = false;
                fake.reset();
            }
        }
        for (std::size_t i = 0; i < impl_->music_slots.size(); ++i) {
            auto& slot = impl_->music_slots[i];
            audio::FakeTrack& fake = impl_->mixer.music[i];
            if (slot.active && !fake.playing && (!ma_sound_is_playing(&slot.sound) || ma_sound_at_end(&slot.sound))) {
                ma_sound_uninit(&slot.sound);
                slot.active = false;
            }
        }
    }
#endif
    impl_->mixer.tick(dt);
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        for (auto it = impl_->looping_voices.begin(); it != impl_->looping_voices.end();) {
            if (impl_->mixer.looping.find(it->first) == impl_->mixer.looping.end()) {
                if (it->second.active) {
                    ma_sound_uninit(&it->second.sound);
                }
                it = impl_->looping_voices.erase(it);
            } else {
                ++it;
            }
        }
    }
#endif
}

void AudioSystem::play_sfx(const Sound& sound, float volume_scale) {
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
    if (impl_->engine_inited && sound.clip != nullptr) {
        const std::string& path = audio_get_path(*sound.clip);
        if (!path.empty()) {
            const std::size_t index = static_cast<std::size_t>(track - impl_->mixer.sfx.data());
            auto& voice_slot = impl_->sfx_voices[index];
            if (voice_slot.active) {
                ma_sound_uninit(&voice_slot.sound);
                voice_slot.active = false;
            }
            ma_result res = ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                                   MA_SOUND_FLAG_DECODE, &impl_->sfx_group, nullptr, &voice_slot.sound);
            if (res == MA_SUCCESS) {
                voice_slot.active = true;
                ma_sound_set_volume(&voice_slot.sound, voice);
                ma_sound_set_pitch(&voice_slot.sound, ratio);
                ma_sound_set_looping(&voice_slot.sound, sound.loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_spatialization_enabled(&voice_slot.sound, MA_FALSE);
                ma_sound_start(&voice_slot.sound);
            }
        }
    }
#endif
}

void AudioSystem::play_spatial_sfx(const Sound& sound, const SpatialAudioParams& params, float volume_scale) {
    audio::FakeTrack* const track = impl_->mixer.acquire_sfx();
    if (track == nullptr) {
        return;
    }
    const float distance = glm::distance(impl_->mixer.listener_position, params.position);
    const float att = audio::calculate_attenuation(params.attenuation, distance, params.min_distance, params.max_distance, params.rolloff);
    const float voice = sound.volume * volume_scale;
    const float gain = impl_->sfx_gain(voice * att);
    const float ratio = pitch_ratio(sound);
    track->start(gain, ratio, sound.loop, voice);
    track->is_spatial = true;
    track->spatial = params;
    ++impl_->mixer.sfx_play_count;
    impl_->mixer.last_sfx_gain = gain;

#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited && sound.clip != nullptr) {
        const std::string& path = audio_get_path(*sound.clip);
        if (!path.empty()) {
            const std::size_t index = static_cast<std::size_t>(track - impl_->mixer.sfx.data());
            auto& voice_slot = impl_->sfx_voices[index];
            if (voice_slot.active) {
                ma_sound_uninit(&voice_slot.sound);
                voice_slot.active = false;
            }
            ma_result res = ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                                   MA_SOUND_FLAG_DECODE, &impl_->sfx_group, nullptr, &voice_slot.sound);
            if (res == MA_SUCCESS) {
                voice_slot.active = true;
                ma_sound_set_volume(&voice_slot.sound, voice);
                ma_sound_set_pitch(&voice_slot.sound, ratio);
                ma_sound_set_looping(&voice_slot.sound, sound.loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_spatialization_enabled(&voice_slot.sound, MA_TRUE);
                ma_sound_set_position(&voice_slot.sound, params.position.x, params.position.y, params.position.z);
                ma_sound_set_velocity(&voice_slot.sound, params.velocity.x, params.velocity.y, params.velocity.z);
                ma_sound_set_direction(&voice_slot.sound, params.direction.x, params.direction.y, params.direction.z);
                ma_sound_set_min_distance(&voice_slot.sound, params.min_distance);
                ma_sound_set_max_distance(&voice_slot.sound, params.max_distance);
                ma_sound_set_rolloff(&voice_slot.sound, params.rolloff);
                ma_sound_set_attenuation_model(&voice_slot.sound, to_ma_attenuation(params.attenuation));
                ma_sound_set_doppler_factor(&voice_slot.sound, params.doppler_factor);
                ma_sound_start(&voice_slot.sound);
            }
        }
    }
#endif
}

void AudioSystem::set_listener(const glm::vec3& position,
                               const glm::vec3& forward,
                               const glm::vec3& up,
                               const glm::vec3& velocity) {
    impl_->mixer.listener_position = position;
    impl_->mixer.listener_forward = forward;
    impl_->mixer.listener_up = up;
    impl_->mixer.listener_velocity = velocity;

#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
        ma_engine_listener_set_velocity(&impl_->engine, 0, velocity.x, velocity.y, velocity.z);
    }
#endif
}

void AudioSystem::play_music(const Sound& sound, bool loop, float fade_seconds) {
    auto& mixer = impl_->mixer;
    const float target = impl_->music_gain(sound.volume);
    const float ratio = pitch_ratio(sound);

    int incoming = 0;
    int outgoing = -1;

    if (!mixer.any_music_playing()) {
        mixer.active_music_index = 0;
        mixer.music[1].reset();
        mixer.music[0].start(target, ratio, loop, sound.volume);
        incoming = 0;
    } else {
        outgoing = mixer.active_music_index;
        incoming = 1 - outgoing;
        mixer.active_music_index = incoming;

        if (fade_seconds <= 0.f) {
            mixer.music[static_cast<std::size_t>(outgoing)].reset();
            mixer.music[static_cast<std::size_t>(incoming)].start(target, ratio, loop, sound.volume);
        } else {
            mixer.music[static_cast<std::size_t>(incoming)].start(0.f, ratio, loop, sound.volume);
            mixer.music[static_cast<std::size_t>(incoming)].start_fade(target, fade_seconds, false);
            mixer.music[static_cast<std::size_t>(outgoing)].start_fade(0.f, fade_seconds, true);
        }
    }

#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited && sound.clip != nullptr) {
        const std::string& path = audio_get_path(*sound.clip);
        if (!path.empty()) {
            if (outgoing >= 0) {
                auto& out_slot = impl_->music_slots[static_cast<std::size_t>(outgoing)];
                if (out_slot.active) {
                    if (fade_seconds <= 0.f) {
                        ma_sound_stop(&out_slot.sound);
                        ma_sound_uninit(&out_slot.sound);
                        out_slot.active = false;
                    } else {
                        ma_sound_set_fade_in_milliseconds(&out_slot.sound, -1.f, 0.f, static_cast<ma_uint64>(fade_seconds * 1000.f));
                    }
                }
            }
            auto& in_slot = impl_->music_slots[static_cast<std::size_t>(incoming)];
            if (in_slot.active) {
                ma_sound_uninit(&in_slot.sound);
                in_slot.active = false;
            }
            ma_result res = ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                                   MA_SOUND_FLAG_STREAM, &impl_->music_group, nullptr, &in_slot.sound);
            if (res == MA_SUCCESS) {
                in_slot.active = true;
                in_slot.path = path;
                ma_sound_set_looping(&in_slot.sound, loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_pitch(&in_slot.sound, ratio);
                ma_sound_set_spatialization_enabled(&in_slot.sound, MA_FALSE);
                if (fade_seconds <= 0.f || outgoing < 0) {
                    ma_sound_set_volume(&in_slot.sound, sound.volume);
                } else {
                    ma_sound_set_volume(&in_slot.sound, 0.f);
                    ma_sound_set_fade_in_milliseconds(&in_slot.sound, 0.f, sound.volume, static_cast<ma_uint64>(fade_seconds * 1000.f));
                }
                ma_sound_start(&in_slot.sound);
            }
        }
    }
#endif
}

void AudioSystem::stop_music(float fade_seconds) {
    for (audio::FakeTrack& track : impl_->mixer.music) {
        if (!track.playing) {
            continue;
        }
        track.start_fade(0.f, fade_seconds, true);
    }
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        for (auto& slot : impl_->music_slots) {
            if (slot.active) {
                if (fade_seconds <= 0.f) {
                    ma_sound_stop(&slot.sound);
                    ma_sound_uninit(&slot.sound);
                    slot.active = false;
                } else {
                    ma_sound_set_fade_in_milliseconds(&slot.sound, -1.f, 0.f, static_cast<ma_uint64>(fade_seconds * 1000.f));
                }
            }
        }
    }
#endif
}

bool AudioSystem::is_music_playing() const {
    return impl_->mixer.any_music_playing();
}

LoopingSfxHandle AudioSystem::create_looping_sfx() {
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

void AudioSystem::play_looping_sfx(LoopingSfxHandle handle, const Sound& sound, float fade_in) {
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
    if (impl_->engine_inited && sound.clip != nullptr) {
        const std::string& path = audio_get_path(*sound.clip);
        if (!path.empty()) {
            auto& voice_slot = impl_->looping_voices[handle.id];
            if (voice_slot.active) {
                ma_sound_uninit(&voice_slot.sound);
                voice_slot.active = false;
            }
            ma_result res = ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                                   MA_SOUND_FLAG_DECODE, &impl_->sfx_group, nullptr, &voice_slot.sound);
            if (res == MA_SUCCESS) {
                voice_slot.active = true;
                voice_slot.path = path;
                ma_sound_set_looping(&voice_slot.sound, MA_TRUE);
                ma_sound_set_pitch(&voice_slot.sound, ratio);
                ma_sound_set_spatialization_enabled(&voice_slot.sound, track->is_spatial ? MA_TRUE : MA_FALSE);
                if (track->is_spatial) {
                    const auto& p = track->spatial;
                    ma_sound_set_position(&voice_slot.sound, p.position.x, p.position.y, p.position.z);
                    ma_sound_set_velocity(&voice_slot.sound, p.velocity.x, p.velocity.y, p.velocity.z);
                    ma_sound_set_direction(&voice_slot.sound, p.direction.x, p.direction.y, p.direction.z);
                    ma_sound_set_min_distance(&voice_slot.sound, p.min_distance);
                    ma_sound_set_max_distance(&voice_slot.sound, p.max_distance);
                    ma_sound_set_rolloff(&voice_slot.sound, p.rolloff);
                    ma_sound_set_attenuation_model(&voice_slot.sound, to_ma_attenuation(p.attenuation));
                    ma_sound_set_doppler_factor(&voice_slot.sound, p.doppler_factor);
                }
                if (fade_in <= 0.f) {
                    ma_sound_set_volume(&voice_slot.sound, voice);
                } else {
                    ma_sound_set_volume(&voice_slot.sound, 0.f);
                    ma_sound_set_fade_in_milliseconds(&voice_slot.sound, 0.f, voice, static_cast<ma_uint64>(fade_in * 1000.f));
                }
                ma_sound_start(&voice_slot.sound);
            }
        }
    }
#endif
}

void AudioSystem::set_looping_sfx_spatial(LoopingSfxHandle handle, const SpatialAudioParams& params) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    track->is_spatial = true;
    track->spatial = params;

#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        auto it = impl_->looping_voices.find(handle.id);
        if (it != impl_->looping_voices.end() && it->second.active) {
            ma_sound_set_spatialization_enabled(&it->second.sound, MA_TRUE);
            ma_sound_set_position(&it->second.sound, params.position.x, params.position.y, params.position.z);
            ma_sound_set_velocity(&it->second.sound, params.velocity.x, params.velocity.y, params.velocity.z);
            ma_sound_set_direction(&it->second.sound, params.direction.x, params.direction.y, params.direction.z);
            ma_sound_set_min_distance(&it->second.sound, params.min_distance);
            ma_sound_set_max_distance(&it->second.sound, params.max_distance);
            ma_sound_set_rolloff(&it->second.sound, params.rolloff);
            ma_sound_set_attenuation_model(&it->second.sound, to_ma_attenuation(params.attenuation));
            ma_sound_set_doppler_factor(&it->second.sound, params.doppler_factor);
        }
    }
#endif
}

void AudioSystem::stop_looping_sfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    track->start_fade(0.f, fade_out, true);
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        auto it = impl_->looping_voices.find(handle.id);
        if (it != impl_->looping_voices.end() && it->second.active) {
            if (fade_out <= 0.f) {
                ma_sound_stop(&it->second.sound);
                ma_sound_uninit(&it->second.sound);
                it->second.active = false;
            } else {
                ma_sound_set_fade_in_milliseconds(&it->second.sound, -1.f, 0.f, static_cast<ma_uint64>(fade_out * 1000.f));
            }
        }
    }
#endif
}

void AudioSystem::release_looping_sfx(LoopingSfxHandle handle, float fade_out) {
    audio::FakeTrack* const track = impl_->mixer.looping_track(handle);
    if (track == nullptr) {
        return;
    }
    if (fade_out <= 0.f) {
        impl_->mixer.looping.erase(handle.id);
#ifdef ENGINE_WITH_AUDIO
        if (impl_->engine_inited) {
            auto it = impl_->looping_voices.find(handle.id);
            if (it != impl_->looping_voices.end()) {
                if (it->second.active) {
                    ma_sound_stop(&it->second.sound);
                    ma_sound_uninit(&it->second.sound);
                }
                impl_->looping_voices.erase(it);
            }
        }
#endif
        return;
    }
    track->release_when_fade_done = true;
    track->start_fade(0.f, fade_out, true);
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        auto it = impl_->looping_voices.find(handle.id);
        if (it != impl_->looping_voices.end() && it->second.active) {
            ma_sound_set_fade_in_milliseconds(&it->second.sound, -1.f, 0.f, static_cast<ma_uint64>(fade_out * 1000.f));
        }
    }
#endif
}

void AudioSystem::set_master_volume(float volume) {
    impl_->master = clamp01(volume);
    impl_->update_music_gains();
    impl_->update_sfx_gains();
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        ma_engine_set_volume(&impl_->engine, impl_->master);
    }
#endif
}

void AudioSystem::set_music_volume(float volume) {
    impl_->music_bus = clamp01(volume);
    impl_->update_music_gains();
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        ma_sound_group_set_volume(&impl_->music_group, impl_->music_bus);
    }
#endif
}

void AudioSystem::set_sfx_volume(float volume) {
    impl_->sfx_bus = clamp01(volume);
    impl_->update_sfx_gains();
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        ma_sound_group_set_volume(&impl_->sfx_group, impl_->sfx_bus);
    }
#endif
}

void AudioSystem::stop_all() {
    audio::FakeMixer& mixer = impl_->mixer;
    for (audio::FakeTrack& track : mixer.sfx) {
        track.reset();
    }
    for (audio::FakeTrack& track : mixer.music) {
        track.reset();
    }
    mixer.active_music_index = 0;
    mixer.looping.clear();
#ifdef ENGINE_WITH_AUDIO
    if (impl_->engine_inited) {
        for (auto& voice : impl_->sfx_voices) {
            if (voice.active) {
                ma_sound_stop(&voice.sound);
                ma_sound_uninit(&voice.sound);
                voice.active = false;
            }
        }
        for (auto& slot : impl_->music_slots) {
            if (slot.active) {
                ma_sound_stop(&slot.sound);
                ma_sound_uninit(&slot.sound);
                slot.active = false;
            }
        }
        for (auto& [id, voice] : impl_->looping_voices) {
            if (voice.active) {
                ma_sound_stop(&voice.sound);
                ma_sound_uninit(&voice.sound);
            }
        }
        impl_->looping_voices.clear();
    }
#endif
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

glm::vec3 AudioSystem::listener_position() const {
    return impl_->mixer.listener_position;
}

glm::vec3 AudioSystem::listener_forward() const {
    return impl_->mixer.listener_forward;
}

glm::vec3 AudioSystem::listener_up() const {
    return impl_->mixer.listener_up;
}

} // namespace engine
