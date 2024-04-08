---
tags: [module]
---

# Audio

Lumenwake-shaped API on SDL3_mixer when `ENGINE_WITH_AUDIO`, otherwise a fake mixer so tests have no device.

## Capabilities

- One-shot SFX pool (`kSfxPoolSize = 12`); skip when full.
- Music A/B slots (`kMusicSlotCount = 2`), fade, loop flag.
- Looping SFX handles (create / play / stop / release).
- Buses: master, music, sfx. `final_gain = clamp(master * bus * voice)`.
- Game trigger: `PlaySfxEvent` / `PlayMusicEvent` on the world ([[include.engine.audio.events.h]]). Engine Audio phase `get<Sound>` and calls the system.

## How it is implemented

- [[include.engine.audio.audio_system.h]] — `IAudioSystem` + `AudioSystem`.
- [[src.audio.audio_system.cpp]] — real or fake behind `#if ENGINE_WITH_AUDIO`.
- [[src.audio.clip.cpp]] / [[src.audio.clip.h]] — decoded clip wrapper.
- [[src.audio.fake_mixer.h]] — test double.
- [[include.engine.audio.sound.h]] — CPU asset (`get<Sound>`).

CMake enables **WAVE only** (no FLAC/Vorbis/MP3). Games convert other formats as needed.

`Host::tick` and `EngineRuntime::run` call `Update(dt)` for fades.

## Public headers

- [[include.engine.audio.audio_system.h]]
- [[include.engine.audio.sound.h]]
- [[include.engine.audio.events.h]]

## Tests

[[tests.audio_test.cpp]] — fake mixer, no `MIX_Init`.

## See also

- [[features/Audio]]
- [[architecture/Runtime Loop]]
- [[build/CMake]]
