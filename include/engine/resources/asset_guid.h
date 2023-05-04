#pragma once

#include <filesystem>

namespace engine {

// Write sidecar `.meta` for assets that do not have one. Never overwrites. Returns how many files were created.
[[nodiscard]] int write_missing_metas(const std::filesystem::path& root);

}
