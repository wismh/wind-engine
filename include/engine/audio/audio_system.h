#pragma once

#include <engine/audio/sound.h>

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

    virtual bool Init() = 0;
    virtual void Dispose() = 0;
    virtual void Update(float dt) = 0;

    virtual void PlaySfx(const Sound& sound, float volume_scale = 1.f) = 0;

    virtual void PlayMusic(const Sound& sound, bool loop = true, float fade_seconds = 0.f) = 0;
    virtual void StopMusic(float fade_seconds = 0.f) = 0;
    virtual bool IsMusicPlaying() const = 0;

    virtual LoopingSfxHandle CreateLoopingSfx() = 0;
    virtual void PlayLoopingSfx(LoopingSfxHandle handle, const Sound& sound, float fade_in = 0.f) = 0;
    virtual void StopLoopingSfx(LoopingSfxHandle handle, float fade_out = 0.f) = 0;
    virtual void ReleaseLoopingSfx(LoopingSfxHandle handle, float fade_out = 0.f) = 0;

    virtual void SetMasterVolume(float volume) = 0;
    virtual void SetMusicVolume(float volume) = 0;
    virtual void SetSfxVolume(float volume) = 0;
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

}

class AudioSystem final : public IAudioSystem {
public:
    AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) noexcept;
    AudioSystem& operator=(AudioSystem&&) noexcept;
    ~AudioSystem() override;

    bool Init() override;
    void Dispose() override;
    void Update(float dt) override;

    void PlaySfx(const Sound& sound, float volume_scale = 1.f) override;

    void PlayMusic(const Sound& sound, bool loop = true, float fade_seconds = 0.f) override;
    void StopMusic(float fade_seconds = 0.f) override;
    bool IsMusicPlaying() const override;

    LoopingSfxHandle CreateLoopingSfx() override;
    void PlayLoopingSfx(LoopingSfxHandle handle, const Sound& sound, float fade_in = 0.f) override;
    void StopLoopingSfx(LoopingSfxHandle handle, float fade_out = 0.f) override;
    void ReleaseLoopingSfx(LoopingSfxHandle handle, float fade_out = 0.f) override;

    void SetMasterVolume(float volume) override;
    void SetMusicVolume(float volume) override;
    void SetSfxVolume(float volume) override;

    [[nodiscard]] int sfx_pool_size() const;
    [[nodiscard]] int sfx_playing_count() const;
    [[nodiscard]] int sfx_play_count() const;
    [[nodiscard]] float last_sfx_gain() const;

    [[nodiscard]] int active_music_index() const;
    [[nodiscard]] bool music_slot_playing(int slot) const;
    [[nodiscard]] float music_slot_gain(int slot) const;

    [[nodiscard]] int looping_track_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
