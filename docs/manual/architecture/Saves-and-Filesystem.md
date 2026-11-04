# Saves & Filesystem

Game save files, persistent configurations, and user profile data must be stored in standardized user directories rather than the working directory of the binary.

Wind provides `engine::user_data_directory` to abstract platform-specific writable storage paths safely.

---

## 1. Using `user_data_directory`

```cpp
#include <engine/core/platform.h>
#include <engine/log.h>

std::expected<std::filesystem::path, std::error_code> path_res =
    engine::user_data_directory("MyStudio", "SuperGame");

if (!path_res) {
    engine::log::warn("Could not obtain user data directory: " + path_res.error().message());
    // Fall back to memory-only or temporary directory
} else {
    std::filesystem::path save_dir = *path_res;
    std::filesystem::path save_file = save_dir / "savegame.json";
    // Read or write save_file
}
```

---

## 2. Platform Locations

| Platform | Location | Description |
| --- | --- | --- |
| **Windows** | `%APPDATA%\MyStudio\SuperGame\` | Standard Windows Roaming AppData folder. |
| **Linux / BSD** | `~/.local/share/MyStudio/SuperGame/` | Adheres to `$XDG_DATA_HOME` specification. |
| **macOS** | `~/Library/Application Support/MyStudio/SuperGame/` | Standard macOS application support storage. |
| **Web (WASM)** | `/storage/MyStudio/SuperGame/` | Backed by browser IndexedDB (persists across page reloads). |
| **Android** | `<internal_storage>/user/` | Isolated private app storage beside the staged `assets/`. The two names are ignored there; the application id isolates apps. |

---

## 3. Critical Rules for Saves

### Rule 1: Valid Path Identifiers
Both `organization` and `application` strings are one path segment each and must:
- Be valid UTF-8 and non-empty.
- Have no leading or trailing space, and no trailing dot.
- Contain none of `/ \ : * ? " < > |` or ASCII control characters.
- Not be a Windows device name (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`), also with a suffix such as `CON.txt`.

A bad name returns `std::errc::invalid_argument`. A dedicated server, built without SDL, gets the same directory as a game of the same names. An engine built without a window on Web or Android returns `function_not_supported`, and a directory the system cannot create returns `io_error`.

### Rule 2: Never Rename an Existing Identifier
The strings passed to `user_data_directory` define the persistent storage location. Renaming either argument will leave previous save files orphaned in the old folder.

### Rule 3: Main Thread Execution
Always call `user_data_directory` from the main thread once the engine is initialized (in `on_start` or later), as underlying platform APIs (SDL / Android JNI) require an active subsystem instance.

### Rule 4: Graceful Degradation
If `user_data_directory` returns an error (e.g. read-only filesystem or sandboxed environment):
- Log a warning.
- Do not crash or terminate the process.
- Fall back to default in-memory settings (reads return defaults, writes gracefully no-op).

---

## 4. Compressing Saves and Packets

`<engine/core/compression.h>` packs bytes with LZ4. It pairs with `engine::net::ByteWriter` and `ByteReader`, which use the same `std::uint8_t` bytes:

```cpp
#include <engine/core/compression.h>
#include <engine/net/byte_stream.h>

// Saving a region: High packs smaller and reads back as fast as Fast.
engine::net::ByteWriter writer;
write_region(writer, region);
const std::vector<std::uint8_t> packed = engine::compress(writer.bytes(), engine::CompressionLevel::High);

// Loading it: the second argument is the largest size you accept.
constexpr std::size_t kMaxRegionBytes = 16u << 20;
const auto unpacked = engine::decompress(packed, kMaxRegionBytes);
if (!unpacked) {
    engine::log::warn(std::format("region is damaged: {}", engine::to_string(unpacked.error())));
    return;
}
engine::net::ByteReader reader{*unpacked};
```

- Use `CompressionLevel::Fast` (the default) for anything per frame, such as chunk packets, and `High` for files.
- Always pass a real `max_size`. A save from disk or a packet from the network can claim any size, and `decompress` refuses one above your limit before it allocates (`CompressionError::TooLarge`).
- There is no checksum. A damaged file can decode into wrong bytes, so validate what you read, or add a hash to your save format.
- Both functions are safe to call from a background job (`JobSystem::submit`): pack a region on a worker, then write the file on the main thread. They are the only engine calls a job may make.
- The 5-byte header is stable, so saves written by one engine version load in the next.

---

## Next Steps

- Keep settings in a TOML file with [`engine::Config`](Config-and-Random.md).
- Proceed to the [Entities & Components](../ecs/Entities-and-Components.md) chapter to start structuring game logic.
- Learn about [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md) for immutable game resources.
