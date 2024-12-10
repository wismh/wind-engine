#include <engine/haptics/haptics_system.h>

#include "fake_haptics.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL_system.h>

#include <jni.h>
#endif

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

}

#if defined(__EMSCRIPTEN__)
namespace {

EM_JS(int, web_vibrate_supported, (), {
    return (typeof navigator !== 'undefined' && typeof navigator.vibrate === 'function') ? 1 : 0;
});

EM_JS(void, web_vibrate, (double ms), {
    if (typeof navigator !== 'undefined' && typeof navigator.vibrate === 'function') {
        navigator.vibrate(ms);
    }
});

EM_JS(void, web_vibrate_cancel, (), {
    if (typeof navigator !== 'undefined' && typeof navigator.vibrate === 'function') {
        navigator.vibrate(0);
    }
});

}
#endif

struct HapticsSystem::Impl {
    haptics::FakeHaptics fake;

#if defined(__ANDROID__)
    JNIEnv* env = nullptr;
    jobject vibrator = nullptr;
    jclass vibrator_class = nullptr;
    jclass effect_class = nullptr;
    jmethodID has_vibrator = nullptr;
    jmethodID vibrate_legacy = nullptr;
    jmethodID vibrate_effect = nullptr;
    jmethodID cancel_method = nullptr;
    jmethodID create_one_shot = nullptr;
    bool ready = false;

    ~Impl() {
        release();
    }

    void release() {
        if (env != nullptr) {
            if (vibrator != nullptr) {
                env->DeleteGlobalRef(vibrator);
            }
            if (vibrator_class != nullptr) {
                env->DeleteGlobalRef(vibrator_class);
            }
            if (effect_class != nullptr) {
                env->DeleteGlobalRef(effect_class);
            }
        }
        vibrator = nullptr;
        vibrator_class = nullptr;
        effect_class = nullptr;
        has_vibrator = nullptr;
        vibrate_legacy = nullptr;
        vibrate_effect = nullptr;
        cancel_method = nullptr;
        create_one_shot = nullptr;
        ready = false;
    }

    // Resolved once from init(), on the engine's own main thread — Context/Vibrator/
    // VibrationEffect are Android framework classes visible via FindClass from any
    // properly-attached thread, so lazy per-call resolution is unnecessary.
    [[nodiscard]] bool resolve() {
        release();
        env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env == nullptr || activity == nullptr) {
            env = nullptr;
            return false;
        }
        if (env->PushLocalFrame(16) != 0) {
            return false;
        }

        jclass context_class = env->GetObjectClass(activity);
        const jmethodID get_system_service =
                env->GetMethodID(context_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring service_name = env->NewStringUTF("vibrator");
        jobject local_vibrator = env->CallObjectMethod(activity, get_system_service, service_name);
        jclass local_vibrator_class = env->FindClass("android/os/Vibrator");

        if (local_vibrator == nullptr || local_vibrator_class == nullptr) {
            env->PopLocalFrame(nullptr);
            return false;
        }

        vibrator = env->NewGlobalRef(local_vibrator);
        vibrator_class = static_cast<jclass>(env->NewGlobalRef(local_vibrator_class));
        has_vibrator = env->GetMethodID(vibrator_class, "hasVibrator", "()Z");
        vibrate_legacy = env->GetMethodID(vibrator_class, "vibrate", "(J)V");
        cancel_method = env->GetMethodID(vibrator_class, "cancel", "()V");

        if (SDL_GetAndroidSDKVersion() >= 26) {
            jclass local_effect_class = env->FindClass("android/os/VibrationEffect");
            if (local_effect_class != nullptr) {
                effect_class = static_cast<jclass>(env->NewGlobalRef(local_effect_class));
                vibrate_effect = env->GetMethodID(vibrator_class, "vibrate", "(Landroid/os/VibrationEffect;)V");
                create_one_shot =
                        env->GetStaticMethodID(effect_class, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
            }
        }

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->PopLocalFrame(nullptr);

        ready = vibrator != nullptr && vibrator_class != nullptr && has_vibrator != nullptr &&
                vibrate_legacy != nullptr && cancel_method != nullptr;
        return ready;
    }

    [[nodiscard]] bool query_has_vibrator() {
        if (!ready) {
            return false;
        }
        const jboolean result = env->CallBooleanMethod(vibrator, has_vibrator);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return result == JNI_TRUE;
    }

    void request(float duration_seconds, float intensity) {
        if (!ready) {
            return;
        }
        const auto ms = static_cast<jlong>(std::lround(static_cast<double>(duration_seconds) * 1000.0));
        if (vibrate_effect != nullptr && create_one_shot != nullptr && SDL_GetAndroidSDKVersion() >= 26) {
            // amplitude 0 throws IllegalArgumentException — vibrate() already rejects
            // intensity <= 0 before reaching here, so this clamp only guards rounding.
            const int amplitude = std::clamp(static_cast<int>(std::lround(intensity * 255.f)), 1, 255);
            if (env->PushLocalFrame(4) == 0) {
                jobject effect = env->CallStaticObjectMethod(effect_class, create_one_shot, ms, amplitude);
                if (effect != nullptr) {
                    env->CallVoidMethod(vibrator, vibrate_effect, effect);
                }
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                env->PopLocalFrame(nullptr);
            }
            return;
        }
        env->CallVoidMethod(vibrator, vibrate_legacy, ms);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    void request_cancel() {
        if (!ready) {
            return;
        }
        env->CallVoidMethod(vibrator, cancel_method);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
#endif
};

HapticsSystem::HapticsSystem()
    : impl_(std::make_unique<Impl>()) {}

HapticsSystem::HapticsSystem(HapticsSystem&&) noexcept = default;
HapticsSystem& HapticsSystem::operator=(HapticsSystem&&) noexcept = default;

HapticsSystem::~HapticsSystem() {
    dispose();
}

bool HapticsSystem::init() {
    if (!impl_) {
        return false;
    }
    impl_->fake.reset();
#if defined(__ANDROID__)
    impl_->resolve();
#endif
    return true;
}

void HapticsSystem::dispose() {
    if (!impl_) {
        return;
    }
#if defined(__EMSCRIPTEN__)
    web_vibrate_cancel();
#elif defined(__ANDROID__)
    impl_->request_cancel();
    impl_->release();
#endif
    impl_->fake.reset();
}

void HapticsSystem::vibrate(float duration_seconds, float intensity) {
    const float clamped_intensity = clamp01(intensity);
    if (duration_seconds <= 0.f || clamped_intensity <= 0.f) {
        return;
    }
    impl_->fake.start(duration_seconds, clamped_intensity);
#if defined(__EMSCRIPTEN__)
    web_vibrate(static_cast<double>(duration_seconds) * 1000.0);
#elif defined(__ANDROID__)
    impl_->request(duration_seconds, clamped_intensity);
#endif
}

void HapticsSystem::cancel() {
    impl_->fake.stop();
#if defined(__EMSCRIPTEN__)
    web_vibrate_cancel();
#elif defined(__ANDROID__)
    impl_->request_cancel();
#endif
}

bool HapticsSystem::is_supported() const {
#if defined(__EMSCRIPTEN__)
    return web_vibrate_supported() != 0;
#elif defined(__ANDROID__)
    return impl_->query_has_vibrator();
#else
    return false;
#endif
}

bool HapticsSystem::is_active() const {
    return impl_->fake.active;
}

float HapticsSystem::last_duration_seconds() const {
    return impl_->fake.last_duration_seconds;
}

float HapticsSystem::last_intensity() const {
    return impl_->fake.last_intensity;
}

int HapticsSystem::vibrate_call_count() const {
    return impl_->fake.vibrate_call_count;
}

int HapticsSystem::cancel_call_count() const {
    return impl_->fake.cancel_call_count;
}

}
