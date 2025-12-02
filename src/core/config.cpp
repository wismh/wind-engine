#include <engine/core/config.h>

#include <engine/core/platform.h>

#include <toml++/toml.hpp>

#include <cassert>
#include <fstream>
#include <sstream>
#include <system_error>

namespace engine {

namespace {

bool valid_part(std::string_view part) noexcept {
    if (part.empty()) {
        return false;
    }
    for (const char c : part) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string temp_name(const std::filesystem::path& path) {
    return path.filename().string() + ".tmp";
}

}

bool Config::valid_key(std::string_view key) noexcept {
    const std::size_t dot = key.find('.');
    if (dot == std::string_view::npos) {
        return valid_part(key);
    }
    return valid_part(key.substr(0, dot)) && valid_part(key.substr(dot + 1));
}

template<typename T>
const T* Config::find(std::string_view key) const {
    const auto it = values_.find(key);
    return it == values_.end() ? nullptr : std::get_if<T>(&it->second);
}

bool Config::has(std::string_view key) const {
    return values_.find(key) != values_.end();
}

std::int64_t Config::get_int(std::string_view key, std::int64_t fallback) const {
    const auto* value = find<std::int64_t>(key);
    return value != nullptr ? *value : fallback;
}

double Config::get_float(std::string_view key, double fallback) const {
    if (const auto* value = find<double>(key)) {
        return *value;
    }
    if (const auto* value = find<std::int64_t>(key)) {
        return static_cast<double>(*value);
    }
    return fallback;
}

bool Config::get_bool(std::string_view key, bool fallback) const {
    const auto* value = find<bool>(key);
    return value != nullptr ? *value : fallback;
}

std::string Config::get_string(std::string_view key, std::string_view fallback) const {
    const auto* value = find<std::string>(key);
    return value != nullptr ? *value : std::string(fallback);
}

bool Config::conflicts(std::string_view key) const {
    if (const std::size_t dot = key.find('.'); dot != std::string_view::npos) {
        return values_.find(key.substr(0, dot)) != values_.end();
    }
    const std::string prefix = std::string(key) + ".";
    const auto it = values_.lower_bound(prefix);
    return it != values_.end() && it->first.starts_with(prefix);
}

void Config::set(std::string_view key, Value value) {
    assert(valid_key(key) && !conflicts(key));
    if (!valid_key(key) || conflicts(key)) {
        return;
    }
    values_.insert_or_assign(std::string(key), std::move(value));
}

void Config::set_int(std::string_view key, std::int64_t value) {
    set(key, value);
}

void Config::set_float(std::string_view key, double value) {
    set(key, value);
}

void Config::set_bool(std::string_view key, bool value) {
    set(key, value);
}

void Config::set_string(std::string_view key, std::string_view value) {
    set(key, std::string(value));
}

bool Config::remove(std::string_view key) {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return false;
    }
    values_.erase(it);
    return true;
}

std::expected<Config, ConfigError> Config::load(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        return std::unexpected(ConfigError::Io);
    }
    if (!exists) {
        return Config{};
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::unexpected(ConfigError::Io);
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(ConfigError::Io);
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
        return std::unexpected(ConfigError::Io);
    }

    toml::table table;
    try {
        table = toml::parse(text.str());
    } catch (const toml::parse_error&) {
        return std::unexpected(ConfigError::InvalidToml);
    }

    Config config;
    const auto read_scalar = [&](const std::string& key, const toml::node& node) {
        if (const auto* value = node.as_integer()) {
            config.values_.insert_or_assign(key, static_cast<std::int64_t>(value->get()));
        } else if (const auto* value = node.as_floating_point()) {
            config.values_.insert_or_assign(key, value->get());
        } else if (const auto* value = node.as_boolean()) {
            config.values_.insert_or_assign(key, value->get());
        } else if (const auto* value = node.as_string()) {
            config.values_.insert_or_assign(key, value->get());
        } else {
            return false;
        }
        return true;
    };
    for (const auto& [name, node] : table) {
        const std::string root(name.str());
        if (!valid_part(root)) {
            return std::unexpected(ConfigError::Unsupported);
        }
        if (const auto* section = node.as_table()) {
            for (const auto& [child, leaf] : *section) {
                const std::string leaf_name(child.str());
                if (!valid_part(leaf_name) || !read_scalar(root + "." + leaf_name, leaf)) {
                    return std::unexpected(ConfigError::Unsupported);
                }
            }
        } else if (!read_scalar(root, node)) {
            return std::unexpected(ConfigError::Unsupported);
        }
    }
    return config;
}

std::expected<void, ConfigError> Config::save(const std::filesystem::path& path) const {
    toml::table table;
    for (const auto& [key, value] : values_) {
        toml::table* target = &table;
        std::string leaf = key;
        if (const std::size_t dot = key.find('.'); dot != std::string::npos) {
            const std::string section = key.substr(0, dot);
            leaf = key.substr(dot + 1);
            if (table.get(section) == nullptr) {
                table.insert(section, toml::table{});
            }
            target = table.get(section)->as_table();
        }
        std::visit([&](const auto& v) { target->insert_or_assign(leaf, v); }, value);
    }

    std::ostringstream text;
    text << table << '\n';

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(ConfigError::Io);
        }
    }
    const std::filesystem::path temp = path.parent_path() / temp_name(path);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected(ConfigError::Io);
        }
        out << text.str();
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(temp, ec);
            return std::unexpected(ConfigError::Io);
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return std::unexpected(ConfigError::Io);
    }
    return {};
}

std::expected<std::filesystem::path, std::error_code> user_config_path(
        std::string_view organization, std::string_view application, std::string_view file_name) {
    const auto directory = user_data_directory(organization, application);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    return *directory / std::string(file_name);
}

}
