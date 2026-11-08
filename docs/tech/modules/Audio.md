# Audio

`IAudioSystem` plays `Sound` assets. `ENGINE_WITH_AUDIO` links the `miniaudio` backend with support for WAV, MP3, FLAC, and OGG Vorbis. Without that flag, `AudioSystem` uses the fake mixer in `src/audio/fake_mixer.h`, so tests never open a device.

## API

`include/engine/audio/audio_system.h`

| Call | Behavior |
| --- | --- |
| `play_sfx(sound, volume_scale = 1)` | One-shot 2D SFX. Skipped when the pool is full |
| `play_spatial_sfx(sound, params, volume_scale = 1)` | One-shot 3D SFX positioned in world space with distance attenuation |
| `set_listener(position, forward, up, velocity)` | Updates 3D listener transform and velocity for spatialization and Doppler |
| `play_music(sound, loop = true, fade_seconds = 0)` | Plays on the idle music slot. `fade_seconds > 0` fades that slot in and previous slot out |
| `stop_music(fade_seconds = 0)` | Fades the active music slot out |
| `is_music_playing` | True while a music slot is audible |
| `create_looping_sfx` | Handle. `id == 0` is invalid |
| `play_looping_sfx` / `stop_looping_sfx` / `release_looping_sfx` | Start, fade out, or free a looping voice |
| `set_looping_sfx_spatial(handle, params)` | Updates 3D position, velocity, and attenuation params of a looping SFX |
| `set_master_volume` / `set_music_volume` / `set_sfx_volume` | Bus gains |
| `stop_all` | Stops every SFX voice and both music slots at once, without a fade, and releases every looping handle |

`kSfxPoolSize` is 12. `kMusicSlotCount` is 2.

`final_gain` clamps `master * bus * voice_volume` to `[0, 1]`.

`update(dt)` advances fades and voice lifecycles. `simulate_worlds` calls it once per frame with the clamped process `real_dt`, not one world's `Time`. `Host::tick` passes the same pointer through. `enable_audio` reads that world's audio events into the shared device.

Introspection used by tests (`sfx_pool_size`, `sfx_playing_count`, `sfx_play_count`, `last_sfx_gain`, music slot gain, `looping_track_count`, `listener_position`) reads the fake model. It is not a hardware query.

## 3D Spatial Audio

Spatial parameters are defined in `include/engine/audio/spatial.h` via `SpatialAudioParams`:
- `position`, `velocity`, `direction`
- `min_distance`, `max_distance`, `rolloff`
- `attenuation`: `AttenuationModel::Inverse`, `AttenuationModel::Linear`, or `AttenuationModel::Exponential`
- `doppler_factor`

### ECS Integration

1. **Listener (`AudioListener`)**:
   Add `AudioListener` to the camera or player entity. The audio system reads its `WorldTransform` (or `Transform`), automatically computes forward and up vectors, and sets the listener coordinates.
2. **One-shot 3D Events (`PlaySpatialSfxEvent`)**:
   Send via `ecs::EventWriter<PlaySpatialSfxEvent>{world}.send(...)` with world-space coordinates.
3. **Continuous 3D Emitter (`AudioSource3D`)**:
   Component attached to an entity (e.g., torch, engine, enemy). The audio system tracks its world position each frame and updates the looping voice.

## Assets and events

A `Sound` (`include/engine/audio/sound.h`) is an `Audio` clip plus volume, `pitch_range`, `loop`, and `AudioBank` (`Sfx` or `Music`). Miniaudio handles loading, decoding, and streaming under `src/audio/`. Public headers never include 3rd-party audio headers.

Import settings live on the `.meta` (`volume`, `pitch_min`, `pitch_max`, `loop`, `bank`). One file is one `Sound`. Supported formats include WAV, MP3, FLAC, and OGG.

The game sends events:

```cpp
ecs::EventWriter<PlaySfxEvent>{world}.send(PlaySfxEvent{.id = assets::audio::hit, .volume_scale = 1.f});
ecs::EventWriter<PlaySpatialSfxEvent>{world}.send(PlaySpatialSfxEvent{
    .id = assets::audio::explosion,
    .params = {.position = glm::vec3(10.f, 0.f, 5.f)}
});
```

`PlayMusicEvent` carries `id`, `loop` (default true), and `fade_seconds`.

The Audio-phase system (`run_audio`) reads each event once through `ctx<EventCursor<…>>()`, `get<Sound>`, and calls `play_sfx`, `play_spatial_sfx`, or `play_music`. A null `AssetsDb` or `IAudioSystem` skips the event.

Web requires a user gesture before audio starts (`audio_requires_user_gesture()`).

## CMake

`ENGINE_WITH_AUDIO` is OFF at the engine root and ON when Wind is added as a subdirectory. Presets: `vs-audio`, `web-audio`. The definition on the `engine` target is `PRIVATE`. Builds `external/miniaudio` as `wind_miniaudio`.

## Files

- `include/engine/audio/audio_system.h`
- `include/engine/audio/audio_listener.h`
- `include/engine/audio/audio_source_3d.h`
- `include/engine/audio/spatial.h`
- `include/engine/audio/sound.h`
- `include/engine/audio/events.h`
- `src/audio/audio_system.cpp`
- `src/audio/clip.h`, `src/audio/clip.cpp`
- `src/audio/fake_mixer.h`

## Tests

`tests/audio_test.cpp` uses the fake mixer. It never calls `AudioSystem::init`, so it opens no device even with `ENGINE_WITH_AUDIO` (the `vs-editor` preset): a constructed `AudioSystem` without `init` runs the same pool, bus, 3D attenuation, and fade logic with no mixer behind it.

## See also

- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [CMake](../build/CMake.md)
- [Assets](../features/Assets.md)
