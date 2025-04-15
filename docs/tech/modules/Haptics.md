# Haptics

Device vibration: duration and intensity only. One frontend, `IHaptics`. The backend is chosen at compile time inside `HapticsSystem`. There is no `ENGINE_WITH_HAPTICS` option and no third-party library.

## API

`include/engine/haptics/haptics_system.h`

| Call | Contract |
| --- | --- |
| `init` / `dispose` | `Engine::init` calls `haptics_->init()` and, on failure, shuts the runtime down and returns false. `haptics_->dispose()` runs from `Engine::dispose`, and only after `initialized_` was set |
| `vibrate(duration_seconds, intensity = 1)` | Fire-and-forget. Intensity is clamped to `[0, 1]` |
| `cancel` | Stops a vibration already running |
| `is_supported` | Runtime capability, not "this binary was compiled for Android" |

`duration_seconds <= 0` or clamped intensity `<= 0` requests nothing. A vibration already running keeps running. `vibrate` does not cancel.

There is no `update(dt)`. The OS or the browser times the pulse.

Test counters (`is_active`, `last_duration_seconds`, `last_intensity`, `vibrate_call_count`, `cancel_call_count`) read the fake model, not a motor.

## Backends

`src/haptics/haptics_system.cpp` branches inside `Impl`:

| Build | Behavior |
| --- | --- |
| `__EMSCRIPTEN__` | `EM_JS` around `navigator.vibrate(ms)`. Any intensity above 0 is full strength. `is_supported` checks that the function exists (Firefox removed it; Safari and iOS never had it) |
| `__ANDROID__` | JNI `Context.getSystemService("vibrator")` once in `init`. API 26+ uses `VibrationEffect.createOneShot` (real amplitude). API 21–25 uses `vibrate(long)` and ignores amplitude |
| Otherwise | No-op. `is_supported()` is false |

`src/haptics/fake_haptics.h` records the request on every backend, including the real ones, so tests do not need a device.

`haptics_has_amplitude_control(Platform::Android)` is the compile-time simplification in `platform.h`. It does not know the API 26 check.

## Public header

`include/engine/haptics/haptics_system.h`

## Tests

`tests/haptics_test.cpp`

## See also

- [Core](Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
