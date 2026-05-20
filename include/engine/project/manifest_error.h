#pragma once

// docs/tech/modules/Project.md

#include <string>
#include <string_view>

namespace engine {

enum class ManifestError {
    // The file does not exist or cannot be read.
    Missing,
    // Not valid TOML.
    Malformed,
    // A required key is absent.
    MissingKey,
    // A key holds another TOML type than the one it needs.
    WrongType,
};

struct ManifestFailure {
    ManifestError kind = ManifestError::Missing;
    // The path, the parser message, or the key.
    std::string detail;
};

[[nodiscard]] std::string_view to_string(ManifestError error) noexcept;
// One line for a status bar: "<what failed>: <detail>".
[[nodiscard]] std::string describe(const ManifestFailure& failure);

}
