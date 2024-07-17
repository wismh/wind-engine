#pragma once

#include <memory>

namespace engine {

// Device vibration: duration + intensity only. One frontend API — the active backend
// (no-op / Web navigator.vibrate / Android JNI Vibrator) is fully hidden from game code.
//
// Cross-platform intensity degradation:
//   - Native (desktop): true no-op, no hardware. is_supported() == false.
//   - Web:               navigator.vibrate(ms) is pure on/off - no amplitude control.
//                        Any intensity > 0 buzzes at full strength for duration_seconds.
//                        is_supported() is a REAL runtime check (does navigator.vibrate
//                        exist?), not "compiled for Web -> yes" (Firefox removed it,
//                        Safari/iOS never had it).
//   - Android API 26+:  real amplitude control (VibrationEffect.createOneShot).
//   - Android API 21-25: legacy Vibrator.vibrate(long); amplitude ignored.
//
// Contract:
//   - intensity is clamped to [0, 1].
//   - duration_seconds <= 0, or clamped intensity <= 0, is a no-op: nothing is requested
//     and any vibration already running from an earlier call keeps running - vibrate()
//     never implicitly cancels; call cancel() explicitly.
//   - No per-frame update(): calls are fire-and-forget, timed by the OS/browser.
class IHaptics {
public:
    virtual ~IHaptics() = default;

    virtual bool init() = 0;
    virtual void dispose() = 0;

    virtual void vibrate(float duration_seconds, float intensity = 1.f) = 0;
    virtual void cancel() = 0;

    virtual bool is_supported() const = 0;
};

class HapticsSystem final : public IHaptics {
public:
    HapticsSystem();
    HapticsSystem(const HapticsSystem&) = delete;
    HapticsSystem& operator=(const HapticsSystem&) = delete;
    HapticsSystem(HapticsSystem&&) noexcept;
    HapticsSystem& operator=(HapticsSystem&&) noexcept;
    ~HapticsSystem() override;

    bool init() override;
    void dispose() override;
    void vibrate(float duration_seconds, float intensity = 1.f) override;
    void cancel() override;
    bool is_supported() const override;

    // Test-only introspection (plain public methods, matching AudioSystem::sfx_playing_count()
    // style) - reflects the fake model's belief about the most recent request, not real
    // hardware timing (there is no update()).
    [[nodiscard]] bool is_active() const;
    [[nodiscard]] float last_duration_seconds() const;
    [[nodiscard]] float last_intensity() const;
    [[nodiscard]] int vibrate_call_count() const;
    [[nodiscard]] int cancel_call_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
