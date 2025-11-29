#pragma once

// docs/tech/modules/Core.md

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace engine {

enum class ConfigError {
    Io,           // the file exists but could not be read, or could not be written
    InvalidToml,  // the file is not valid TOML
    Unsupported,  // valid TOML, but not a flat table or one level of tables of strings, ints, floats, and bools
};

[[nodiscard]] constexpr std::string_view to_string(ConfigError error) noexcept {
    switch (error) {
        case ConfigError::Io:
            return "Io";
        case ConfigError::InvalidToml:
            return "InvalidToml";
        case ConfigError::Unsupported:
            return "Unsupported";
    }
    return "Unknown";
}

// Settings of a game: strings, ints, floats, and bools by key, stored as a TOML file. A key is `name` or
// `section.name` (one level: `audio.volume` is `volume` in `[audio]`); each part is non-empty ASCII letters, digits,
// `_` and `-`. The file is a plain TOML file a player can edit. tomlplusplus stays private to the engine.
//
//   auto config = engine::Config::load(path).value_or(engine::Config{});
//   const int volume = config.get_int("audio.volume", 80);
//   config.set_int("audio.volume", 60);
//   (void)config.save(path);
//
// A getter returns the default when the key is missing or holds another type (`get_float` also reads an int).
// A setter with an invalid key, or a key that is both a value and a section (`audio` and `audio.volume`), is a
// programmer error (`assert`) and writes nothing; check with `valid_key` when the key is data.
// Main thread only.
class Config {
public:
    // A missing file is an empty config. A file that cannot be parsed is an error and nothing is changed on disk.
    [[nodiscard]] static std::expected<Config, ConfigError> load(const std::filesystem::path& path);

    // Writes `path.tmp` beside the file and renames it over `path`, so a failure leaves the old file as it was.
    // Creates missing parent directories.
    [[nodiscard]] std::expected<void, ConfigError> save(const std::filesystem::path& path) const;

    [[nodiscard]] static bool valid_key(std::string_view key) noexcept;

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] std::int64_t get_int(std::string_view key, std::int64_t fallback) const;
    [[nodiscard]] double get_float(std::string_view key, double fallback) const;
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback) const;
    [[nodiscard]] std::string get_string(std::string_view key, std::string_view fallback) const;

    void set_int(std::string_view key, std::int64_t value);
    void set_float(std::string_view key, double value);
    void set_bool(std::string_view key, bool value);
    void set_string(std::string_view key, std::string_view value);

    // Returns whether the key existed.
    bool remove(std::string_view key);

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

private:
    using Value = std::variant<std::int64_t, double, bool, std::string>;

    template<typename T>
    [[nodiscard]] const T* find(std::string_view key) const;
    // `audio` (a value) and `audio.volume` (a value in the table `audio`) cannot both exist.
    [[nodiscard]] bool conflicts(std::string_view key) const;
    void set(std::string_view key, Value value);

    std::map<std::string, Value, std::less<>> values_;
};

// `<user data directory of organization/application>/<file_name>` (default `config.toml`), the file `Config::load`
// and `save` usually take. Fails like `user_data_directory`.
[[nodiscard]] std::expected<std::filesystem::path, std::error_code> user_config_path(
        std::string_view organization, std::string_view application, std::string_view file_name = "config.toml");

}
