# Haptics

Wind provides device vibration through `engine::IHaptics`: a duration and an intensity, nothing more. It suits collisions, weapon recoil, and UI taps on phones.

---

## 1. Triggering Vibrations

Keep `services.haptics` (an `engine::IHaptics&`) and call `vibrate()`. The duration is in **seconds**:

```cpp
#include <engine/core/engine_services.h>
#include <engine/haptics/haptics_system.h>

void trigger_hit_feedback(engine::IHaptics& haptics) {
    // Vibrate for 150 milliseconds at 80% intensity
    haptics.vibrate(0.15f, 0.8f);
}
```

- `intensity` is clamped to `[0, 1]` (default `1`).
- A duration `<= 0`, or a clamped intensity `<= 0`, requests nothing. A vibration that is already running keeps running: `vibrate()` never cancels one. Call `haptics.cancel()` for that.
- Calls are fire-and-forget. There is no per-frame update.
- `haptics.is_supported()` asks the device at run time. Use it to hide a "Vibration" setting.

---

## 2. Platform Support & Intensity

- **Android:** Uses the system `Vibrator`. On Android 8.0+ (API 26+) the intensity is a real amplitude. On API 21 to 25 the amplitude is ignored (on/off).
- **Web:** Uses `navigator.vibrate(ms)` where the browser has it. It is on/off: any intensity above 0 buzzes at full strength. Firefox removed it and Safari/iOS never had it, so `is_supported()` is false there.
- **Desktop:** No hardware backend. `is_supported()` is false and `vibrate()` does nothing. Gamepad rumble is not supported.

On a device without support, `vibrate()` is a harmless no-op.

---

## Next Steps

- Set up resource cooking in [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
- Type-safe asset retrieval with [Loading Assets](../assets-and-loc/Loading-Assets.md).
