#pragma once

// docs/tech/modules/Audio.md

#include <engine/audio/sound.h>
#include <engine/audio/spatial.h>

#include <cmath>
#include <cstdint>
#include <memory>

namespace engine {

struct LoopingSfxHandle {
    std::uint32_t id = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0;
    }

    constexpr bool operator==(const LoopingSfxHandle&) const noexcept = default;
};

class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    virtual bool init() = 0;
    virtual void dispose() = 0;
    virtual void update(float dt) = 0;

    virtual void play_sfx(const Sound& sound, float volume_scale = 1.f) = 0;
    virtual void play_spatial_sfx(const Sound& sound,
                                  const SpatialAudioParams& params,
                                  float volume_scale = 1.f) = 0;

    virtual void set_listener(const glm::vec3& position,
                              const glm::vec3& forward = {0.f, 0.f, -1.f},
                              const glm::vec3& up = {0.f, 1.f, 0.f},
                              const glm::vec3& velocity = {0.f, 0.f, 0.f}) = 0;

    virtual void play_music(const Sound& sound, bool loop = true, float fade_seconds = 0.f) = 0;
    virtual void stop_music(float fade_seconds = 0.f) = 0;
    virtual bool is_music_playing() const = 0;

    virtual LoopingSfxHandle create_looping_sfx() = 0;
    virtual void play_looping_sfx(LoopingSfxHandle handle, const Sound& sound, float fade_in = 0.f) = 0;
    virtual void set_looping_sfx_spatial(LoopingSfxHandle handle, const SpatialAudioParams& params) = 0;
    virtual void stop_looping_sfx(LoopingSfxHandle handle, float fade_out = 0.f) = 0;
    virtual void release_looping_sfx(LoopingSfxHandle handle, float fade_out = 0.f) = 0;

    virtual void set_master_volume(float volume) = 0;
    virtual void set_music_volume(float volume) = 0;
    virtual void set_sfx_volume(float volume) = 0;

    // Silences music and every SFX voice now, without a fade, and releases every looping handle.
    // Volumes stay. The device stays open. The editor calls this on Stop.
    virtual void stop_all() = 0;
};

namespace audio {

inline constexpr int kSfxPoolSize = 12;
inline constexpr int kMusicSlotCount = 2;

[[nodiscard]] constexpr float final_gain(float master, float bus, float voice_volume) {
    const float gain = master * bus * voice_volume;
    if (gain < 0.f) {
        return 0.f;
    }
    if (gain > 1.f) {
        return 1.f;
    }
    return gain;
}

[[nodiscard]] inline float calculate_attenuation(AttenuationModel model,
                                                 float distance,
                                                 float min_distance,
                                                 float max_distance,
                                                 float rolloff) {
    if (distance <= min_distance) {
        return 1.f;
    }
    if (min_distance >= max_distance) {
        return distance <= min_distance ? 1.f : 0.f;
    }
    switch (model) {
    case AttenuationModel::Linear: {
        if (distance >= max_distance) {
            return 0.f;
        }
        return 1.f - (distance - min_distance) / (max_distance - min_distance);
    }
    case AttenuationModel::Inverse: {
        return min_distance / (min_distance + rolloff * (distance - min_distance));
    }
    case AttenuationModel::Exponential: {
        const float ratio = distance / min_distance;
        return std::pow(ratio, -rolloff);
    }
    }
    return 1.f;
}

} // namespace audio

class AudioSystem final : public IAudioSystem {
public:
    AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) noexcept;
    AudioSystem& operator=(AudioSystem&&) noexcept;
    ~AudioSystem() override;

    bool init() override;
    void dispose() override;
    void update(float dt) override;

    void play_sfx(const Sound& sound, float volume_scale = 1.f) override;
    void play_spatial_sfx(const Sound& sound,
                          const SpatialAudioParams& params,
                          float volume_scale = 1.f) override;

    void set_listener(const glm::vec3& position,
                      const glm::vec3& forward = {0.f, 0.f, -1.f},
                      const glm::vec3& up = {0.f, 1.f, 0.f},
                      const glm::vec3& velocity = {0.f, 0.f, 0.f}) override;

    void play_music(const Sound& sound, bool loop = true, float fade_seconds = 0.f) override;
    void stop_music(float fade_seconds = 0.f) override;
    bool is_music_playing() const override;

    LoopingSfxHandle create_looping_sfx() override;
    void play_looping_sfx(LoopingSfxHandle handle, const Sound& sound, float fade_in = 0.f) override;
    void set_looping_sfx_spatial(LoopingSfxHandle handle, const SpatialAudioParams& params) override;
    void stop_looping_sfx(LoopingSfxHandle handle, float fade_out = 0.f) override;
    void release_looping_sfx(LoopingSfxHandle handle, float fade_out = 0.f) override;

    void set_master_volume(float volume) override;
    void set_music_volume(float volume) override;
    void set_sfx_volume(float volume) override;

    void stop_all() override;

    [[nodiscard]] int sfx_pool_size() const;
    [[nodiscard]] int sfx_playing_count() const;
    [[nodiscard]] int sfx_play_count() const;
    [[nodiscard]] float last_sfx_gain() const;

    [[nodiscard]] int active_music_index() const;
    [[nodiscard]] bool music_slot_playing(int slot) const;
    [[nodiscard]] float music_slot_gain(int slot) const;

    [[nodiscard]] int looping_track_count() const;

    [[nodiscard]] glm::vec3 listener_position() const;
    [[nodiscard]] glm::vec3 listener_forward() const;
    [[nodiscard]] glm::vec3 listener_up() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
