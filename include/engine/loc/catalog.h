#pragma once

// docs/tech/modules/Localization.md

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace engine::loc {

// A named value for `{name}` and for the number a `{count, plural, ...}` clause selects on.
// The string alternative is a view: the caller keeps the characters alive for the call.
struct Arg {
    std::string_view name;
    std::variant<std::int64_t, std::string_view> value;
};

enum class Role {
    Translation,
    Source,
};

// One locale's messages. `messages` maps a stable key (`menu.play`) to a pattern.
struct StringTable {
    std::string locale;
    std::unordered_map<std::string, std::string> messages;
};

enum class StringTableError {
    InvalidToml,
    MissingLocale,
    EmptyId,
    DuplicateId,
    MissingText,
    BadPattern,
};

struct StringTableFailure {
    StringTableError kind = StringTableError::InvalidToml;
    std::string detail;
};

// TOML body of a `.strings` asset. `locale` is required. Each `[[string]]` needs `id` and `text`.
// `note` is kept for translators and ignored here. A pattern that is not valid message syntax
// fails the whole table.
[[nodiscard]] std::expected<StringTable, StringTableFailure> parse_string_table(std::string_view toml);

struct Translated {
    std::string text;
    // True when the key is absent from the table added with Role::Source. The active locale
    // missing a key that the source table has is a fallback, not this flag.
    bool missing_from_source = false;
};

// Loaded tables plus the locale the game has selected. Lives in `world.ctx<Catalog>()`;
// `ctx()` default-constructs an empty one, so a game that never calls `add` is unchanged.
//
// `text` looks up active, then fallback, then the key itself. A missing active translation
// warns once. A key absent from the source table warns once and sets `missing_from_source`.
// `set_pseudo` wraps the finished string in brackets and lengthens it.
class Catalog {
public:
    void add(StringTable table, Role role = Role::Translation);
    void set_active(std::string locale);
    void set_fallback(std::string locale);
    void set_pseudo(bool enabled);

    [[nodiscard]] std::string_view active() const noexcept;
    [[nodiscard]] std::string_view fallback() const noexcept;
    [[nodiscard]] bool pseudo() const noexcept;
    // Distinct warn sites so far (one per key per reason). A repeated lookup does not grow it.
    [[nodiscard]] std::size_t warning_count() const noexcept;

    [[nodiscard]] Translated text(std::string_view key, std::span<const Arg> args = {}) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tables_;
    std::string source_locale_;
    std::string active_;
    std::string fallback_;
    bool pseudo_ = false;
    mutable std::unordered_set<std::string> warned_;
};

}
