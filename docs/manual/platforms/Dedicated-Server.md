# Dedicated Server

A dedicated server is a separate program of your game: no window, no renderer, no UI, no sound, and no SDL. It runs your simulation on the fixed step, sleeps to the tick rate, logs to stdout, and stops cleanly on Ctrl+C or `SIGTERM`. Clients are your normal game with a `net::GnsTransport` and `net::install_net_client`.

---

## 1. The Target

Add a second target beside `engine_add_game`:

```cmake
find_package(Wind REQUIRED)

engine_add_game(my_game
    src/main.cpp
    src/game.cpp
    src/sim.cpp)

engine_add_server(my_server
    src/server_main.cpp
    src/server.cpp
    src/sim.cpp)
```

Put the simulation both targets share in its own files. The server target has no window headers: `<engine/game_entry.h>` and `<engine/core/engine.h>` do not compile there, and a shared file that includes them will tell you at once.

---

## 2. The Entry Point and the Server Class

```cpp
// src/server_main.cpp
#include <engine/server_entry.h>

#include "server.h"

ENGINE_GAME_SERVER(game::Server)
```

A server is an `IGame` built from `const ServerServices&`, which holds assets, HTTP, processes, jobs, worlds, the fatal hook, the command line, and the config file. It has no graphics, input, or audio.

```cpp
class Server final : public engine::GameBase {
public:
    explicit Server(const engine::ServerServices& services)
        : engine::GameBase(services.worlds)
        , fatal_(&services.fatal)
        , port_(services.port(27015)) {}

    void on_start() override {
        engine::net::install_net_server(world(), transport_);
        if (!transport_.start_server(port_)) {
            fatal_->report("Cannot open the server socket");
        }
        // add your Fixed systems here
    }

    void on_quit() override {
        transport_.stop();  // tells every client now, so none waits for a timeout
    }

private:
    engine::IFatalError* fatal_;
    std::uint16_t port_;
    engine::net::GnsTransport transport_;
};
```

To refuse to start, call `services.fatal.report(...)`: it logs the message and exits with code 1. `quit()` inside `on_start` is ignored, as in every Wind host.

---

## 3. Command Line and Config

| Option | Meaning |
| --- | --- |
| `--port <1..65535>` | The port. `services.port(fallback)` answers this, else `port` in the config file, else `fallback` |
| `--config <file>` | A TOML config file, read through `services.config`. Without it, `server.toml` beside the executable when there is one. A missing `--config` file or invalid TOML stops the start |
| `--assets <dir>` | The cooked assets. Default: `assets` beside the executable |
| `--help` | The list above |

Anything else lands in `services.options.args` for your game, in order, as does everything after `--`.

| Exit code | Meaning |
| --- | --- |
| 0 | A clean stop, or `--help` |
| 1 | It could not start, or a fatal error |
| 2 | A bad command line |

---

## 4. What a Server Can Load

The catalogs load as in a game, without a graphics factory. Prototypes, `.world` files, documents, and your own objects load normally. A GPU asset (`try_get<render::IMesh>`) answers `AssetError::NotReady`, and a `.world` that names a mesh or material still instantiates, with nothing behind the reference, so one world file serves both programs.

Saves use `engine::user_data_directory` as in the game. It needs `HOME` or `XDG_DATA_HOME` on Linux, so set one of them under a service manager that does not.

---

## 5. Running It

The executable lands in `bin/<config>/` with `assets/` beside it (and, on Windows, the GameNetworkingSockets DLLs: copy the whole directory to the host).

```bash
cmake --build build-editor --config Release --target my_server
build-editor/bin/Release/my_server --port 27015 --config server.toml
```

**Stopping.** Ctrl+C, `SIGTERM` (`systemctl stop`, `docker stop`), and `SIGHUP` stop it after the current tick: `on_quit` runs and the process exits 0. A second signal ends it at once, in case a shutdown hangs. On Windows a closing console, a logoff, and a system shutdown do the same within about 4.5 s, Windows's limit. `taskkill` without `/F` does not reach a console program with no window; use Ctrl+C or Ctrl+Break.

**Tick rate.** 60 Hz, the engine's `kFixed`. To send less often, set `NetServerConfig::snapshot_interval`. After a stall of more than eight ticks the ticks past the eighth are dropped, and the log says so at most once in ten seconds.

**Two servers on one port.** The Windows build of GameNetworkingSockets does not refuse this: both start, and the clients go to one of them. Give each server its own port.

---

## 6. On Linux

Build it on the Linux machine (or CI) that matches your SDK. A Linux SDK has a Server player only when it was built with the GameNetworkingSockets source; released SDKs are. You need:

```bash
sudo apt install libssl-dev libprotobuf-dev protobuf-compiler
```

protobuf and OpenSSL link statically, so the host that only runs the server does not need them.

**Limits.** On Linux, only the server has networking: a Linux desktop build of your game has no GameNetworkingSockets, and `GnsTransport` fails there. Windows clients against a Linux server have not been tested yet (different builds of one library, version 1.6.0 on Linux). macOS has no server build.

---

## Next Steps

- [Desktop](Desktop.md): exporting the game itself. The editor's Export does not export a server.
- [Saves & Filesystem](../architecture/Saves-and-Filesystem.md).
