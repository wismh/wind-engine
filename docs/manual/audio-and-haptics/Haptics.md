# Haptics

Wind provides force-feedback and vibration support through `engine::IHaptics`, enabling physical tactile responses for collisions, weapon recoil, and UI taps.

---

## 1. Triggering Vibrations

Access `services.haptics` and call `vibrate()`:

```cpp
#include <engine/haptics/haptics_system.h>

void trigger_hit_feedback(engine::EngineServices& services) {
    // Vibrate for 150 milliseconds at 80% intensity
    services.haptics.vibrate(/*duration_ms=*/150, /*intensity=*/0.8f);
}
```

---

## 2. Platform Support & Intensity

- **Mobile (Android):** Maps to Android's `Vibrator` service. On Android 8.0+ (API 26+), variable intensity amplitude is honored.
- **Gamepads (Desktop):** Controls rumble motors in connected gamepads.
- **Web:** Uses the HTML5 Gamepad / Vibration API where permitted by browser security policies.

If haptics are unsupported on the user's device, `vibrate()` degrades gracefully into a harmless no-op.

---

## Next Steps

- Set up resource cooking in [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
- Type-safe asset retrieval with [Loading Assets](../assets-and-loc/Loading-Assets.md).
