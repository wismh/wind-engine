# Settings & Random Numbers

Two small tools that every game ends up writing: a settings file and a random number generator. Both are in the engine, so a game does not parse files by hand and keeps no `std::mt19937` of its own.

---

## 1. Settings with `engine::Config`

`engine::Config` is a table of strings, ints, floats, and bools stored as a TOML file a player can read and edit. Keys are `name` or `section.name` (one level). Each part is made of letters, digits, `_`, and `-`.

```cpp
#include <engine/core/config.h>

// <user data>/MyStudio/SuperGame/config.toml
const auto path = engine::user_config_path("MyStudio", "SuperGame");

engine::Config config;
if (path) {
    if (auto loaded = engine::Config::load(*path)) {
        config = std::move(*loaded);
    } else {
        engine::log::warn(std::string("settings not loaded: ") + std::string(engine::to_string(loaded.error())));
    }
}

const int volume = static_cast<int>(config.get_int("audio.volume", 80));
const bool fullscreen = config.get_bool("video.fullscreen", false);

config.set_int("audio.volume", 60);
config.set_string("player.name", "Ada");
if (path) {
    (void)config.save(*path);
}
```

The file for this config:

```toml
[audio]
volume = 60

[player]
name = "Ada"

[video]
fullscreen = false
```

Rules:

- **A missing file is an empty config.** The first run reads defaults.
- **A getter takes a default.** It returns the default for a missing key and for a key of another type (a `volume = "loud"` the player typed by hand). `get_float` also reads an int.
- **A broken file is an error value.** `load` returns `ConfigError::InvalidToml` for text that is not TOML and `ConfigError::Unsupported` for TOML that is not a flat table of scalars (an array, a date, a table inside a table). Nothing is thrown, and the file on disk is not touched. Keep the defaults, log it, and carry on; the next `save` replaces the file.
- **A save is atomic.** `save` writes `config.toml.tmp` next to the file and renames it over the file. If the write fails (disk full, no permission), the old file is still there and `save` returns `ConfigError::Io`. Missing directories are created.
- **Keys are checked in debug builds.** A setter with an invalid key, or a key that is both a value and a section (`audio` and `audio.volume`), is an `assert` and writes nothing. Check with `Config::valid_key` when the key comes from data.

`user_config_path` is `user_data_directory(...) / "config.toml"` and fails the same way: see [Saves & Filesystem](Saves-and-Filesystem.md) for the platform locations and for what to do when it fails. On Web the file lives in the browser storage, which may flush a few frames later. The engine headers do not expose tomlplusplus.

---

## 2. Random numbers with `engine::Random`

`engine::Random` is PCG32. The same seed gives the same numbers on Windows, Linux, macOS, Web, and Android, because the integer and float mappings are in the engine and not in `<random>` (whose distributions differ between standard libraries). That makes a test, a replay, and a "daily seed" reproducible.

```cpp
auto& random = world().ctx<engine::Random>();

const int roll = random.uniform(1, 6);              // 1..6 inclusive, no modulo bias
const float jitter = random.uniform(-0.5f, 0.5f);  // [-0.5, 0.5)
const bool crit = random.chance(0.1f);              // true 10 % of the time
const auto& tip = random.pick(tips);                // one element of a non-empty vector / array / span
const float unit = random.next_float();             // [0, 1)
```

Every world has one `Random` in its context. By default it is seeded from the clock, so each run differs. To fix the sequence, return a seed from your game:

```cpp
class Game : public engine::GameBase {
public:
    using GameBase::GameBase;

    std::optional<std::uint64_t> random_seed() const override { return 12345; }   // tests, replays
};
```

The engine applies it to the game's world when it attaches the game. A world you create yourself calls `ctx<engine::Random>().seed(n)`. Two independent sequences from one seed use streams: `engine::Random(seed, 1)` and `engine::Random(seed, 2)`, or `seed(seed, stream)`.

Keep the draw order stable if you need a replay to match: a new `uniform` call in the middle of a frame shifts every number after it. For that, give each feature its own `Random` with its own stream.

`Random` is not for cryptography, and like every engine API it is for the main thread.

---

## Next Steps

- Persist progress with [Saves & Filesystem](Saves-and-Filesystem.md).
- Test code that uses randomness with [Testing Game Code](../best-practices/Testing-Game-Code.md).
