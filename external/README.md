# external/

Git submodules. Engine CMake owns these targets. 

## Required before `engine_tests` (cloned in this tree)

| Dir | Repo |
| --- | --- |
| `googletest` | https://github.com/google/googletest.git |
| `glm` | https://github.com/g-truc/glm.git |
| `tomlplusplus` | https://github.com/marzer/tomlplusplus.git |
| `tinyxml2` | https://github.com/leethomason/tinyxml2.git |
| `spdlog` | https://github.com/gabime/spdlog.git |
| `lz4` | https://github.com/lz4/lz4.git (`v1.10.0`; only `lib/lz4.c` and `lib/lz4hc.c` are built, as `wind_lz4`) |

## Required when `ENGINE_WITH_WINDOW=ON`

| Dir | Repo |
| --- | --- |
| `SDL3` | https://github.com/libsdl-org/SDL.git |
| `glad` | vendored generator output or dav1dde/glad |

## Required when `ENGINE_WITH_AUDIO=ON`

| Dir | Repo |
| --- | --- |
| `SDL_mixer` | https://github.com/libsdl-org/SDL_mixer.git (`release-3.2.4`) |

## GameNetworkingSockets

Two copies, by platform. `ENGINE_WITH_GNS` turns the transports on; see `docs/tech/modules/Net.md`.

| Dir | What | Used by |
| --- | --- | --- |
| `GameNetworkingSockets` | Vendored in this repo, not a submodule: headers, import library, and DLLs (GameNetworkingSockets, abseil, protobuf, OpenSSL). Its headers are byte-identical to upstream `v1.5.0`, `v1.5.1`, and `v1.6.0` (interface `SteamNetworkingSockets012`), and differ from `v1.4.1` and `v1.6.1`; which of those three the binaries came from is not recorded | Windows |
| `GameNetworkingSockets-src` | Submodule, https://github.com/ValveSoftware/GameNetworkingSockets.git at tag `v1.6.0` (BSD-3-Clause), `update = none`. Only the static library is built, with OpenSSL and without ICE | Linux, the dedicated server only |

`update = none` keeps it out of `git submodule update --init --recursive`, which would otherwise also clone its nested submodules, webrtc among them, none of which this build needs. Windows never needs it. To build a Linux server, opt in without `--recursive`:

```bash
git -c submodule.external/GameNetworkingSockets-src.update=checkout submodule update --init external/GameNetworkingSockets-src
sudo apt install libssl-dev libprotobuf-dev protobuf-compiler   # Ubuntu, Debian
```

Protobuf and OpenSSL link statically (`Protobuf_USE_STATIC_LIBS`, `OPENSSL_USE_STATIC_LIBS`), so the host that runs the server needs neither. Without the checkout a Linux SDK install warns and has no Server player; `-DENGINE_WITH_GNS=ON` is a configure error.

Limits: a Linux desktop client stays non-networked, and the protocol compatibility of the Windows prebuilt with Linux `v1.6.0` has not been verified.
