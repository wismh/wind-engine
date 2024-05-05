---
tags: [module]
---

# Haptics

Device vibration, duration + intensity only. One frontend API — the active backend (no-op /
Web `navigator.vibrate` / Android JNI `Vibrator`) is fully hidden from game code.

## Capabilities

- `vibrate(duration_seconds, intensity = 1.0)` — one-shot, fire-and-forget.
- `cancel()` — stop an in-progress vibration.
- `is_supported()` — a genuine **runtime** capability check (device has a vibrator motor /
  browser actually implements `navigator.vibrate`), never a "compiled for platform X" guess.
- Intensity is honored as real amplitude only on Android API 26+; everywhere else (Native, Web,
  older Android) it degrades to an on/off gate — see [[include.engine.haptics.haptics_system.h]]
  for the full per-platform table.

## How it is implemented

- [[include.engine.haptics.haptics_system.h]] — `IHaptics` + `HapticsSystem`.
- [[src.haptics.haptics_system.cpp]] — backend dispatch lives *inside* `HapticsSystem::Impl`,
  branched by `#if defined(__EMSCRIPTEN__)` / `#elif defined(__ANDROID__)` / `#else`, not by a
  CMake option (haptics has no third-party library to opt into). Web uses an `EM_JS` shim
  around `navigator.vibrate`; Android resolves `Context.getSystemService("vibrator")` via JNI
  (`SDL_GetAndroidJNIEnv()` / `SDL_GetAndroidActivity()`) once in `init()` and caches global
  refs; Native is a true no-op.
- [[src.haptics.fake_haptics.h]] — always-on state tracker (mirrors `audio::FakeMixer`'s role):
  real backend calls run *alongside* it, never instead of it, so tests never need a device or
  browser.
- No `update(float dt)`: unlike audio's wall-clock fades, vibration calls are fire-and-forget
  and timed by the OS/browser, so nothing needs ticking every frame.

## Public headers

- [[include.engine.haptics.haptics_system.h]]

## Tests

[[tests.haptics_test.cpp]] — fake state model, no device/browser. Plus
[[tests.platform_test.cpp]]'s `HapticsAmplitudeControl` case and
[[tests.cmake_sanity_test.cpp]]'s `AndroidManifestDeclaresVibratePermission` regression check
for the `android.permission.VIBRATE` manifest entry.

## See also

- [[modules/Audio]]
- [[modules/Core]]
- [[architecture/Boundaries]]
