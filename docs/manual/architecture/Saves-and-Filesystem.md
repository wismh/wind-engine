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
    engine::log::warn("Could not obtain user data directory: {}", path_res.error().message());
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
| **Android** | `<internal_storage>/user/` | Isolated private app storage beside cooked runtime assets. |

---

## 3. Critical Rules for Saves

### Rule 1: Valid Path Identifiers
Both `organization` and `application` strings must:
- Be valid UTF-8 and non-empty.
- Contain no leading or trailing whitespace.
- Contain no path separators (`/`, `\`), control characters, or reserved Windows device names (`CON`, `PRN`, `AUX`, `NUL`, etc.).

### Rule 2: Never Rename an Existing Identifier
The strings passed to `user_data_directory` define the persistent storage location. Renaming either argument will leave previous save files orphaned in the old folder.

### Rule 3: Main Thread Execution
Always call `user_data_directory` from the main thread after `app.init()` has succeeded, as underlying platform APIs (SDL / Android JNI) require an active subsystem instance.

### Rule 4: Graceful Degradation
If `user_data_directory` returns an error (e.g. read-only filesystem or sandboxed environment):
- Log a warning.
- Do not crash or terminate the process.
- Fall back to default in-memory settings (reads return defaults, writes gracefully no-op).

---

## Next Steps

- Proceed to the [Entities & Components](../ecs/Entities-and-Components.md) chapter to start structuring game logic.
- Learn about [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md) for immutable game resources.
