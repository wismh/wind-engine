#include "loc/format.h"

#include "loc/plural.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::loc {
namespace {

struct Piece {
    enum class Kind { Literal, Placeholder, Hash, Plural };

    Kind kind = Kind::Literal;
    std::string text;
    std::uint8_t present = 0;
    std::array<std::vector<Piece>, 4> branches{};
};

class Parser {
public:
    explicit Parser(std::string_view text)
        : text_(text) {}

    [[nodiscard]] std::expected<std::vector<Piece>, FormatError> parse_top() {
        auto pieces = parse_sequence(false);
        if (!pieces) {
            return std::unexpected(pieces.error());
        }
        if (i_ != text_.size()) {
            return std::unexpected(FormatError::Unclosed);
        }
        return pieces;
    }

private:
    std::string_view text_;
    std::size_t i_ = 0;

    void skip_ws() {
        while (i_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[i_])) != 0) {
            ++i_;
        }
    }

    [[nodiscard]] bool consume(char ch) {
        if (i_ < text_.size() && text_[i_] == ch) {
            ++i_;
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool is_name_start(unsigned char ch) {
        return std::isalpha(ch) != 0 || ch == '_';
    }

    [[nodiscard]] static bool is_name_cont(unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
    }

    [[nodiscard]] std::expected<std::string, FormatError> read_name() {
        if (i_ >= text_.size() || !is_name_start(static_cast<unsigned char>(text_[i_]))) {
            return std::unexpected(FormatError::BadName);
        }
        const std::size_t begin = i_;
        ++i_;
        while (i_ < text_.size() && is_name_cont(static_cast<unsigned char>(text_[i_]))) {
            ++i_;
        }
        return std::string(text_.substr(begin, i_ - begin));
    }

    [[nodiscard]] static std::optional<PluralCategory> category_of(std::string_view name) {
        if (name == "one") {
            return PluralCategory::One;
        }
        if (name == "few") {
            return PluralCategory::Few;
        }
        if (name == "many") {
            return PluralCategory::Many;
        }
        if (name == "other") {
            return PluralCategory::Other;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::expected<std::vector<Piece>, FormatError> parse_sequence(bool in_branch) {
        std::vector<Piece> pieces;
        std::string literal;
        const auto flush = [&] {
            if (literal.empty()) {
                return;
            }
            Piece piece;
            piece.kind = Piece::Kind::Literal;
            piece.text = std::move(literal);
            pieces.push_back(std::move(piece));
        };

        while (i_ < text_.size()) {
            const char ch = text_[i_];
            if (ch == '{' && i_ + 1 < text_.size() && text_[i_ + 1] == '{') {
                literal.push_back('{');
                i_ += 2;
                continue;
            }
            // A branch ends on the first `}`. The plural's own closer is the next character, so
            // `}}` at the end of the last branch is a close plus a close, not an escaped brace.
            // `}}` is a literal `}` only outside a branch.
            if (ch == '}' && in_branch) {
                flush();
                return pieces;
            }
            if (ch == '}' && i_ + 1 < text_.size() && text_[i_ + 1] == '}') {
                literal.push_back('}');
                i_ += 2;
                continue;
            }
            if (ch == '}') {
                return std::unexpected(FormatError::Unclosed);
            }
            if (ch == '#' && in_branch) {
                flush();
                Piece piece;
                piece.kind = Piece::Kind::Hash;
                pieces.push_back(std::move(piece));
                ++i_;
                continue;
            }
            if (ch == '{') {
                flush();
                auto piece = parse_braced(in_branch);
                if (!piece) {
                    return std::unexpected(piece.error());
                }
                pieces.push_back(std::move(*piece));
                continue;
            }
            literal.push_back(ch);
            ++i_;
        }

        if (in_branch) {
            return std::unexpected(FormatError::Unclosed);
        }
        flush();
        return pieces;
    }

    [[nodiscard]] std::expected<Piece, FormatError> parse_braced(bool in_branch) {
        if (!consume('{')) {
            return std::unexpected(FormatError::Unclosed);
        }
        skip_ws();
        auto name = read_name();
        if (!name) {
            return std::unexpected(name.error());
        }
        skip_ws();
        if (consume('}')) {
            Piece piece;
            piece.kind = Piece::Kind::Placeholder;
            piece.text = std::move(*name);
            return piece;
        }
        if (i_ >= text_.size()) {
            return std::unexpected(FormatError::Unclosed);
        }
        if (!consume(',')) {
            return std::unexpected(FormatError::BadPlural);
        }
        if (in_branch) {
            return std::unexpected(FormatError::NestedPlural);
        }
        skip_ws();
        auto keyword = read_name();
        if (!keyword || *keyword != "plural") {
            return std::unexpected(FormatError::BadPlural);
        }
        skip_ws();
        if (!consume(',')) {
            return std::unexpected(FormatError::BadPlural);
        }

        Piece plural;
        plural.kind = Piece::Kind::Plural;
        plural.text = std::move(*name);
        while (true) {
            skip_ws();
            if (i_ >= text_.size()) {
                return std::unexpected(FormatError::Unclosed);
            }
            if (consume('}')) {
                break;
            }
            auto category_name = read_name();
            if (!category_name) {
                return std::unexpected(FormatError::BadPlural);
            }
            const auto category = category_of(*category_name);
            if (!category) {
                return std::unexpected(FormatError::BadPlural);
            }
            const auto index = static_cast<std::uint8_t>(*category);
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << index);
            if ((plural.present & bit) != 0) {
                return std::unexpected(FormatError::DuplicateCategory);
            }
            skip_ws();
            if (!consume('{')) {
                return std::unexpected(FormatError::BadPlural);
            }
            auto branch = parse_sequence(true);
            if (!branch) {
                return std::unexpected(branch.error());
            }
            if (!consume('}')) {
                return std::unexpected(FormatError::Unclosed);
            }
            plural.present = static_cast<std::uint8_t>(plural.present | bit);
            plural.branches[index] = std::move(*branch);
        }
        const std::uint8_t other_bit = static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(PluralCategory::Other));
        if ((plural.present & other_bit) == 0) {
            return std::unexpected(FormatError::MissingOther);
        }
        return plural;
    }
};

[[nodiscard]] const Arg* find_arg(std::span<const Arg> args, std::string_view name) {
    for (const Arg& arg : args) {
        if (arg.name == name) {
            return &arg;
        }
    }
    return nullptr;
}

void render(const std::vector<Piece>& pieces, std::string& out, std::span<const Arg> args, std::string_view locale,
        std::string_view hash_text) {
    for (const Piece& piece : pieces) {
        switch (piece.kind) {
            case Piece::Kind::Literal:
                out.append(piece.text);
                break;
            case Piece::Kind::Hash:
                out.append(hash_text);
                break;
            case Piece::Kind::Placeholder: {
                const Arg* arg = find_arg(args, piece.text);
                if (arg == nullptr) {
                    out.push_back('{');
                    out.append(piece.text);
                    out.push_back('}');
                    break;
                }
                if (const auto* number = std::get_if<std::int64_t>(&arg->value)) {
                    out.append(std::to_string(*number));
                } else if (const auto* text = std::get_if<std::string_view>(&arg->value)) {
                    out.append(*text);
                }
                break;
            }
            case Piece::Kind::Plural: {
                const Arg* arg = find_arg(args, piece.text);
                const auto* number = arg != nullptr ? std::get_if<std::int64_t>(&arg->value) : nullptr;
                if (number == nullptr) {
                    out.push_back('{');
                    out.append(piece.text);
                    out.push_back('}');
                    break;
                }
                const PluralCategory category = plural_category(locale, *number);
                const auto index = static_cast<std::uint8_t>(category);
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << index);
                const std::vector<Piece>* branch = &piece.branches[index];
                if ((piece.present & bit) == 0) {
                    branch = &piece.branches[static_cast<std::uint8_t>(PluralCategory::Other)];
                }
                render(*branch, out, args, locale, std::to_string(*number));
                break;
            }
        }
    }
}

[[nodiscard]] std::expected<std::vector<Piece>, FormatError> parse_pattern(std::string_view pattern) {
    return Parser{pattern}.parse_top();
}

}

std::expected<void, FormatError> validate_pattern(std::string_view pattern) {
    auto parsed = parse_pattern(pattern);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return {};
}

std::expected<std::string, FormatError> format(std::string_view pattern, std::span<const Arg> args,
        std::string_view locale) {
    auto parsed = parse_pattern(pattern);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    std::string out;
    render(*parsed, out, args, locale, {});
    return out;
}

}
