# Audio

`IAudioSystem` plays `Sound` assets. `ENGINE_WITH_AUDIO` links SDL3_mixer with WAV only. Without that flag, `AudioSystem` uses the fake mixer in `src/audio/fake_mixer.h`, so tests never open a device.

## API

`include/engine/audio/audio_system.h`

| Call | Behavior |
| --- | --- |
| `play_sfx(sound, volume_scale = 1)` | One-shot. Skipped when the pool is full |
| `play_music(sound, loop = true, fade_seconds = 0)` | Plays on the idle music slot. `fade_seconds > 0` fades that slot in and the previous slot out |
| `stop_music(fade_seconds = 0)` | Fades the active slot out |
| `is_music_playing` | True while a music slot is audible |
| `create_looping_sfx` | Handle. `id == 0` is invalid |
| `play_looping_sfx` / `stop_looping_sfx` / `release_looping_sfx` | Start, fade out, or free a looping voice |
| `set_master_volume` / `set_music_volume` / `set_sfx_volume` | Bus gains |
| `stop_all` | Stops every SFX voice and both music slots at once, without a fade, and releases every looping handle. Volumes and the device stay. The editor calls it when a game stops |

`kSfxPoolSize` is 12. `kMusicSlotCount` is 2.

`final_gain` clamps `master * bus * voice_volume` to `[0, 1]`.

`update(dt)` advances fades. `simulate_worlds` calls it once per frame with the clamped process `real_dt`, not one world's `Time`. `Host::tick` passes the same pointer through. `enable_audio` reads that world's `PlaySfxEvent` and `PlayMusicEvent` into the shared device.

Introspection used by tests (`sfx_pool_size`, `sfx_playing_count`, `sfx_play_count`, `last_sfx_gain`, music slot gain, `looping_track_count`) reads the fake model. It is not a hardware query.

## Assets and events

A `Sound` (`include/engine/audio/sound.h`) is a decoded `Audio` clip plus volume, `pitch_range`, `loop`, and `AudioBank` (`Sfx` or `Music`). `MIX_Audio` stays in `src/audio/clip.cpp`. Public headers do not include SDL_mixer.

Import settings live on the `.meta` (`volume`, `pitch_min`, `pitch_max`, `loop`, `bank`). There is no separate cue asset. One file is one `Sound`.

The game sends events. It does not call `play_*` from a filename.

```cpp
ecs::EventWriter<PlaySfxEvent>{world}.send(PlaySfxEvent{.id = assets::audio::hit, .volume_scale = 1.f});
```

`PlayMusicEvent` carries `id`, `loop` (default true), and `fade_seconds`.

The Audio-phase system (`run_audio`) reads each event once through `ctx<EventCursor<…>>()`, `get<Sound>`, and calls `play_sfx` or `play_music`. A null `AssetsDb` or `IAudioSystem` skips the event. `get` is fatal when the id is missing.

Web requires a user gesture before audio starts (`audio_requires_user_gesture()`). The engine does not unlock the context by itself.

## CMake

`ENGINE_WITH_AUDIO` is OFF at the engine root and ON when Wind is added as a subdirectory. Presets: `vs-audio`, `web-audio`. The definition on the `engine` target is `PRIVATE`.

The mixer build sets `SDLMIXER_WAVE` ON and turns FLAC, Vorbis, MP3, MIDI, Opus, AIFF, VOC, AU, GME, MOD, and WavPack OFF.

## Files

- `include/engine/audio/audio_system.h`
- `include/engine/audio/sound.h`
- `include/engine/audio/events.h`
- `src/audio/audio_system.cpp`
- `src/audio/clip.h`, `src/audio/clip.cpp`
- `src/audio/fake_mixer.h`

## Tests

`tests/audio_test.cpp` uses the fake mixer. It never calls `AudioSystem::init`, so it opens no device even with `ENGINE_WITH_AUDIO` (the `vs-editor` preset): a constructed `AudioSystem` without `init` runs the same pool, bus, and fade logic with no mixer behind it.

## See also

- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [CMake](../build/CMake.md)
- [Assets](../features/Assets.md)
