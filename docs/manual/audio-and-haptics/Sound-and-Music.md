# Sound & Music

Wind includes an audio subsystem built on SDL_mixer (`engine::IAudioSystem`) with support for a concurrent sound effects pool, looping audio handles, and smooth music crossfading. The audio build decodes WAV files only.

---

## 1. Playing Sound Effects (SFX)

Load sound clips via `AssetsDb` and trigger one-shot playback using `play_sfx`:

```cpp
#include <engine/audio/audio_system.h>
#include <engine/core/engine_services.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

void play_jump_sound(const engine::EngineServices& services) {
    auto jump_clip = services.assets.get<engine::Sound>(assets::audio::sfx_jump);

    // Play with optional volume scale
    services.audio.play_sfx(*jump_clip, 0.8f);
}
```

The engine owns a fixed-size SFX voice pool (`audio::kSfxPoolSize = 12`). When every voice is busy, a new `play_sfx` is skipped.

The volume, pitch range, `loop`, and bank (`sfx` or `music`) of a `Sound` come from the import settings in its `.meta` ([Asset Pipeline](../assets-and-loc/Asset-Pipeline.md#1-asset-metadata-files-meta)).

### Playing from a system: `PlaySfxEvent` / `PlayMusicEvent`

A gameplay system does not need to hold the audio service. It can send an event, and the engine's audio phase plays it:

```cpp
#include <engine/audio/events.h>
#include <engine/ecs/events.h>

void on_hit(engine::ecs::World& world) {
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(
            engine::PlaySfxEvent{.id = assets::audio::sfx_hit, .volume_scale = 1.0f});
}
```

`PlayMusicEvent` carries `id`, `loop` (default `true`), and `fade_seconds`. The sound is loaded with `get`, so a missing id is fatal.

---

## 2. Background Music & Fading

Control background soundtrack tracks with `play_music` and `stop_music`:

```cpp
auto bgm = services.assets.get<engine::Sound>(assets::audio::music_theme);

// Play looping music with a 1.5 second fade-in
services.audio.play_music(*bgm, /*loop=*/true, /*fade_seconds=*/1.5f);

// Stop playing music with a 2.0 second fade-out
services.audio.stop_music(/*fade_seconds=*/2.0f);

// Is a music slot audible?
const bool playing = services.audio.is_music_playing();
```

The engine has two music slots (`audio::kMusicSlotCount`). A `play_music` with a fade starts on the idle slot, fading it in while the previous track fades out, which gives a crossfade between scenes.

---

## 3. Looping Sound Effects

For continuous audio sources (engine hum, wind ambiance), use `engine::LoopingSfxHandle`:

```cpp
// Create handle once (e.g. in on_start)
engine::LoopingSfxHandle engine_loop = services.audio.create_looping_sfx();

// Start looping sound with fade-in
services.audio.play_looping_sfx(engine_loop, *engine_sound, /*fade_in=*/0.5f);

// Stop looping sound
services.audio.stop_looping_sfx(engine_loop, /*fade_out=*/0.5f);

// Release handle when no longer needed (it can also fade out)
services.audio.release_looping_sfx(engine_loop, /*fade_out=*/0.5f);
```

---

## 4. Volume Buses

Adjust global gain across master, music, and SFX channels independently. The final gain of a voice is `master * bus * voice volume`, clamped to `[0, 1]`:

```cpp
services.audio.set_master_volume(0.9f);
services.audio.set_music_volume(0.7f);
services.audio.set_sfx_volume(1.0f);
```

---

## 5. Web

Browsers keep audio locked until the player interacts with the page (`engine::audio_requires_user_gesture()` is true on Web). The engine does not unlock the audio context itself, so the first sound can be silent until the player has clicked or pressed a key. Start music after the first input, for example from a "Click to start" screen.

---

## Next Steps

- Add tactile feedback with [Haptics](Haptics.md).
- Organize asset metadata with [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
