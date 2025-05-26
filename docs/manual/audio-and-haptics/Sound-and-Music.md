# Sound & Music

Wind includes an audio subsystem built on SDL_mixer (`engine::IAudioSystem`) with support for a concurrent sound effects pool, looping audio handles, and smooth music crossfading.

---

## 1. Playing Sound Effects (SFX)

Load sound clips via `AssetsDb` and trigger one-shot playback using `play_sfx`:

```cpp
#include <engine/audio/audio_system.h>
#include <asset_ids.h>

void play_jump_sound(engine::EngineServices& services) {
    auto jump_clip = services.assets.get<engine::Sound>(assets::audio::sfx_jump);
    
    // Play with optional volume scale [0.0 .. 1.0]
    services.audio.play_sfx(*jump_clip, 0.8f);
}
```

The engine manages a fixed-size SFX voice pool (`kSfxPoolSize = 12`), evicting or reusing channels automatically without cutting off high-priority audio.

---

## 2. Background Music & Fading

Control background soundtrack tracks with `play_music` and `stop_music`:

```cpp
auto bgm = services.assets.get<engine::Sound>(assets::audio::music_theme);

// Play looping music with a 1.5 second fade-in
services.audio.play_music(*bgm, /*loop=*/true, /*fade_seconds=*/1.5f);

// Stop playing music with a 2.0 second fade-out
services.audio.stop_music(/*fade_seconds=*/2.0f);
```

The engine provides 2 music channels internally to allow crossfading between different game scenes.

---

## 3. Looping Sound Effects

For continuous audio sources (engine hum, wind ambiance, footsteps), use `engine::LoopingSfxHandle`:

```cpp
// Create handle once (e.g. in on_start)
engine::LoopingSfxHandle engine_loop = services.audio.create_looping_sfx();

// Start looping sound with fade-in
services.audio.play_looping_sfx(engine_loop, *engine_sound, /*fade_in=*/0.5f);

// Stop looping sound
services.audio.stop_looping_sfx(engine_loop, /*fade_out=*/0.5f);

// Release handle when no longer needed
services.audio.release_looping_sfx(engine_loop);
```

---

## 4. Volume Buses

Adjust global gain across master, music, and SFX channels independently:

```cpp
services.audio.set_master_volume(0.9f);
services.audio.set_music_volume(0.7f);
services.audio.set_sfx_volume(1.0f);
```

---

## Next Steps

- Add tactile feedback with [Haptics](Haptics.md).
- Organize asset metadata with [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
