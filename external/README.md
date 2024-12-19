# external/

Git submodules. Engine CMake owns these targets. **Do not add EnTT.** Do not use FetchContent for these.

## Required before `engine_tests` (cloned in this tree)

| Dir | Repo |
| --- | --- |
| `googletest` | https://github.com/google/googletest.git |
| `glm` | https://github.com/g-truc/glm.git |
| `tomlplusplus` | https://github.com/marzer/tomlplusplus.git |
| `tinyxml2` | https://github.com/leethomason/tinyxml2.git |
| `spdlog` | https://github.com/gabime/spdlog.git |

## Required when `ENGINE_WITH_WINDOW=ON`

| Dir | Repo |
| --- | --- |
| `SDL3` | https://github.com/libsdl-org/SDL.git |
| `glad` | vendored generator output or dav1dde/glad |
| `nanovg` | https://github.com/memononen/nanovg.git |
| `boost_di` | https://github.com/boost-ext/di.git |

Include nanovg as `external/nanovg/src`.

## Required when `ENGINE_WITH_AUDIO=ON`

| Dir | Repo |
| --- | --- |
| `SDL_mixer` | https://github.com/libsdl-org/SDL_mixer.git (`release-3.2.4`) |

WAV only; OGG off (see `docs/tech/architecture/Principles.md`).
