#pragma once

// docs/tech/modules/Project.md#engine-version

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

// An engine version, `<major>.<minor>.<patch><stage>` with an optional `-dev`: "0.1.7a", "0.2.0", "0.1.0a-dev".
// `stage` is 'a' (alpha), 'b' (beta), or 0 (production). The patch has no upper limit. The number is major.minor.patch;
// the stage does not reset it (0.2.5a is followed by 0.2.6b). `dev` is a local build of the engine: it is older than
// every release with the same number. The version of a project (`wind_project.toml: engine`) is compared as the exact
// text; this type is for ordering (the launcher) and for reading what a version string means.
struct EngineVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    char stage = 0;
    bool dev = false;

    // Release order: number, then a `-dev` build before the releases of its number, then alpha, beta, production.
    // 0.1.99a < 0.1.100a < 0.2.0a-dev < 0.2.0a < 0.2.0b < 0.2.0.
    friend std::strong_ordering operator<=>(const EngineVersion& a, const EngineVersion& b) noexcept;
    [[nodiscard]] friend bool operator==(const EngineVersion& a, const EngineVersion& b) noexcept = default;
};

// Reads `^\d+\.\d+\.\d+[ab]?(-dev)?$`. Anything else (a sign, a space, a fourth number, another suffix, a number that
// does not fit an int) is nullopt.
[[nodiscard]] std::optional<EngineVersion> parse_engine_version(std::string_view text);

// The text parse_engine_version reads back to the same value.
[[nodiscard]] std::string to_string(const EngineVersion& version);

}
