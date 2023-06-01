#pragma once

#include <engine/audio/audio_system.h>

#include <array>
#include <cstdint>
#include <unordered_map>

namespace engine::audio {

struct FakeTrack {
    bool playing = false;
    float gain = 0.f;
    float frequency_ratio = 1.f;
    bool loop = false;
    float voice_volume = 1.f;

    float fade_from = 0.f;
    float fade_to = 0.f;
    float fade_duration = 0.f;
    float fade_elapsed = 0.f;
    bool fading = false;
    bool stop_when_fade_done = false;
    bool release_when_fade_done = false;

    void reset() {
        *this = FakeTrack{};
    }

    void start(float start_gain, float ratio, bool looping, float voice) {
        playing = true;
        gain = start_gain;
        frequency_ratio = ratio;
        loop = looping;
        voice_volume = voice;
        fading = false;
        stop_when_fade_done = false;
        release_when_fade_done = false;
        fade_elapsed = 0.f;
        fade_duration = 0.f;
    }

    void start_fade(float to, float duration, bool stop_when_done) {
        if (duration <= 0.f) {
            gain = to;
            fading = false;
            stop_when_fade_done = false;
            if (stop_when_done) {
                playing = false;
                gain = 0.f;
            }
            return;
        }
        fade_from = gain;
        fade_to = to;
        fade_duration = duration;
        fade_elapsed = 0.f;
        fading = true;
        stop_when_fade_done = stop_when_done;
    }

    void tick(float dt) {
        if (!fading) {
            return;
        }
        fade_elapsed += dt;
        if (fade_elapsed >= fade_duration) {
            gain = fade_to;
            fading = false;
            if (stop_when_fade_done) {
                playing = false;
                gain = 0.f;
            }
            return;
        }
        const float t = fade_elapsed / fade_duration;
        gain = fade_from + (fade_to - fade_from) * t;
    }
};

struct FakeMixer {
    std::array<FakeTrack, kSfxPoolSize> sfx{};
    std::array<FakeTrack, kMusicSlotCount> music{};
    std::unordered_map<std::uint32_t, FakeTrack> looping{};
    std::uint32_t next_looping_id = 1;
    int active_music_index = 0;
    int sfx_play_count = 0;
    float last_sfx_gain = 0.f;

    void reset() {
        sfx = {};
        music = {};
        looping.clear();
        next_looping_id = 1;
        active_music_index = 0;
        sfx_play_count = 0;
        last_sfx_gain = 0.f;
    }

    FakeTrack* acquire_sfx() {
        for (FakeTrack& track : sfx) {
            if (!track.playing) {
                return &track;
            }
        }
        return nullptr;
    }

    [[nodiscard]] int sfx_playing_count() const {
        int count = 0;
        for (const FakeTrack& track : sfx) {
            if (track.playing) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool any_music_playing() const {
        for (const FakeTrack& track : music) {
            if (track.playing) {
                return true;
            }
        }
        return false;
    }

    FakeTrack* looping_track(LoopingSfxHandle handle) {
        if (!handle.valid()) {
            return nullptr;
        }
        const auto it = looping.find(handle.id);
        if (it == looping.end()) {
            return nullptr;
        }
        return &it->second;
    }

    void tick(float dt) {
        for (FakeTrack& track : sfx) {
            track.tick(dt);
        }
        for (FakeTrack& track : music) {
            track.tick(dt);
        }
        for (auto& [id, track] : looping) {
            track.tick(dt);
        }
        erase_released_looping();
    }

    void erase_released_looping() {
        for (auto it = looping.begin(); it != looping.end();) {
            if (it->second.release_when_fade_done && !it->second.playing && !it->second.fading) {
                it = looping.erase(it);
            } else {
                ++it;
            }
        }
    }
};

}
