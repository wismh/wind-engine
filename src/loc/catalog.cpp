#include <engine/loc/catalog.h>
#include <engine/log.h>

#include "loc/format.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace engine::loc {
namespace {

std::string pseudolocalize(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 3 + 2);
    out.push_back('[');
    out.append(text);
    const std::size_t extra = std::max<std::size_t>(1, text.size() / 3);
    out.append(extra, '~');
    out.push_back(']');
    return out;
}

const std::unordered_map<std::string, std::string>* find_table(
        const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& tables,
        std::string_view locale) {
    const auto it = tables.find(std::string(locale));
    if (it == tables.end()) {
        return nullptr;
    }
    return &it->second;
}

const std::string* find_pattern(const std::unordered_map<std::string, std::string>* table, std::string_view key) {
    if (table == nullptr) {
        return nullptr;
    }
    const auto it = table->find(std::string(key));
    if (it == table->end()) {
        return nullptr;
    }
    return &it->second;
}

}

std::expected<StringTable, StringTableFailure> parse_string_table(std::string_view toml_text) {
    toml::table table;
    try {
        table = toml::parse(toml_text);
    } catch (const toml::parse_error&) {
        return std::unexpected(StringTableFailure{StringTableError::InvalidToml, "invalid TOML"});
    }

    const auto locale = table["locale"].value<std::string>();
    if (!locale || locale->empty()) {
        return std::unexpected(StringTableFailure{StringTableError::MissingLocale, "locale is missing"});
    }

    StringTable out;
    out.locale = *locale;

    if (!table.contains("string")) {
        return out;
    }
    const toml::array* rows = table["string"].as_array();
    if (rows == nullptr) {
        return std::unexpected(StringTableFailure{StringTableError::InvalidToml, "string must be an array"});
    }

    for (const toml::node& node : *rows) {
        const toml::table* row = node.as_table();
        if (row == nullptr) {
            return std::unexpected(StringTableFailure{StringTableError::InvalidToml, "string entry must be a table"});
        }
        const auto id = (*row)["id"].value<std::string>();
        if (!id || id->empty()) {
            return std::unexpected(StringTableFailure{StringTableError::EmptyId, "string id is empty"});
        }
        if (!row->contains("text")) {
            return std::unexpected(StringTableFailure{StringTableError::MissingText, "string \"" + *id + "\" has no text"});
        }
        const auto message = (*row)["text"].value<std::string>();
        if (!message) {
            return std::unexpected(StringTableFailure{StringTableError::MissingText, "string \"" + *id + "\" has no text"});
        }
        if (out.messages.contains(*id)) {
            return std::unexpected(StringTableFailure{StringTableError::DuplicateId, "duplicate string id \"" + *id + "\""});
        }
        if (auto valid = validate_pattern(*message); !valid) {
            return std::unexpected(
                    StringTableFailure{StringTableError::BadPattern, "string \"" + *id + "\" has a bad pattern"});
        }
        out.messages.emplace(*id, *message);
    }
    return out;
}

void Catalog::add(StringTable table, Role role) {
    if (role == Role::Source) {
        source_locale_ = table.locale;
    }
    tables_[table.locale] = std::move(table.messages);
}

void Catalog::set_active(std::string locale) {
    active_ = std::move(locale);
}

void Catalog::set_fallback(std::string locale) {
    fallback_ = std::move(locale);
}

void Catalog::set_pseudo(bool enabled) {
    pseudo_ = enabled;
}

std::string_view Catalog::active() const noexcept {
    return active_;
}

std::string_view Catalog::fallback() const noexcept {
    return fallback_;
}

bool Catalog::pseudo() const noexcept {
    return pseudo_;
}

std::size_t Catalog::warning_count() const noexcept {
    return warned_.size();
}

Translated Catalog::text(std::string_view key, std::span<const Arg> args) const {
    const std::string* pattern = find_pattern(find_table(tables_, active_), key);
    bool used_fallback = false;
    std::string_view pattern_locale = active_;
    if (pattern == nullptr && !fallback_.empty() && fallback_ != active_) {
        pattern = find_pattern(find_table(tables_, fallback_), key);
        if (pattern != nullptr) {
            used_fallback = true;
            pattern_locale = fallback_;
        }
    }

    Translated out;
    const bool in_source = find_pattern(find_table(tables_, source_locale_), key) != nullptr;
    out.missing_from_source = !in_source;
    if (pattern == nullptr) {
        out.text = std::string(key);
    } else {
        auto formatted = format(*pattern, args, pattern_locale);
        if (!formatted) {
            out.text = *pattern;
            const std::string token = "bad:" + std::string(key);
            if (warned_.insert(token).second) {
                log::warn(std::format("string key \"{}\" has a bad pattern", key));
            }
        } else {
            out.text = std::move(*formatted);
        }
    }

    if (!in_source) {
        const std::string token = "missing:" + std::string(key);
        if (warned_.insert(token).second) {
            log::warn(std::format("missing string key \"{}\"", key));
        }
    } else if (pattern == nullptr) {
        const std::string token = "untranslated:" + std::string(key);
        if (warned_.insert(token).second) {
            log::warn(std::format("string key \"{}\" has no text for locale \"{}\"", key, active_));
        }
    } else if (used_fallback) {
        const std::string token = "fallback:" + std::string(key);
        if (warned_.insert(token).second) {
            log::warn(std::format("string key \"{}\" missing from locale \"{}\"; using \"{}\"", key, active_, fallback_));
        }
    }

    if (pseudo_) {
        out.text = pseudolocalize(out.text);
    }
    return out;
}

}
