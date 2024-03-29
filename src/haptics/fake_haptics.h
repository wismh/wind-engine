#pragma once

namespace engine::haptics {

struct FakeHaptics {
    bool active = false;
    float last_duration_seconds = 0.f;
    float last_intensity = 0.f;
    int vibrate_call_count = 0;
    int cancel_call_count = 0;

    void reset() {
        *this = FakeHaptics{};
    }

    void start(float duration_seconds, float intensity) {
        active = true;
        last_duration_seconds = duration_seconds;
        last_intensity = intensity;
        ++vibrate_call_count;
    }

    void stop() {
        active = false;
        ++cancel_call_count;
    }
};

}
