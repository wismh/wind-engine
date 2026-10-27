#pragma once

// docs/tech/modules/Resources.md

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace engine {

class AssetId {
public:
    static constexpr std::size_t kHexLength = 32;
    static constexpr std::string_view kUriPrefix = "asset://";
    static constexpr std::size_t kUriLength = kUriPrefix.size() + kHexLength;

    constexpr AssetId() noexcept = default;

    explicit constexpr AssetId(std::string_view hex) {
        if (!is_valid_hex(hex)) {
            throw std::invalid_argument("AssetId must be 32 lowercase hex characters");
        }
        for (std::size_t i = 0; i < kHexLength; ++i) {
            hex_[i] = hex[i];
        }
    }

    [[nodiscard]] static constexpr bool is_valid_hex(std::string_view hex) noexcept {
        if (hex.size() != kHexLength) {
            return false;
        }
        for (char c : hex) {
            const bool decimal = c >= '0' && c <= '9';
            const bool lower_hex = c >= 'a' && c <= 'f';
            if (!decimal && !lower_hex) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool is_valid(std::string_view hex) noexcept {
        return is_valid_hex(hex);
    }

    [[nodiscard]] static constexpr bool is_valid_uri(std::string_view uri) noexcept {
        if (uri.size() != kUriLength || !uri.starts_with(kUriPrefix)) {
            return false;
        }
        return is_valid_hex(uri.substr(kUriPrefix.size()));
    }

    [[nodiscard]] static std::optional<AssetId> parse_hex(std::string_view hex) {
        if (!is_valid_hex(hex)) {
            return std::nullopt;
        }
        return AssetId{hex};
    }

    [[nodiscard]] static std::optional<AssetId> parse_uri(std::string_view uri) {
        if (!is_valid_uri(uri)) {
            return std::nullopt;
        }
        return AssetId{uri.substr(kUriPrefix.size())};
    }

    [[nodiscard]] static std::optional<AssetId> parse(std::string_view text) {
        return parse_uri(text);
    }

    [[nodiscard]] constexpr std::string_view hex() const noexcept {
        return std::string_view(hex_.data(), hex_.size());
    }

    [[nodiscard]] std::string uri() const {
        std::string s;
        s.reserve(kUriLength);
        s.append(kUriPrefix);
        s.append(hex());
        return s;
    }

    constexpr auto operator<=>(const AssetId&) const noexcept = default;
    constexpr bool operator==(const AssetId&) const noexcept = default;

private:
    std::array<char, kHexLength> hex_{};
};

} // namespace engine

template<>
struct std::hash<engine::AssetId> {
    std::size_t operator()(const engine::AssetId& id) const noexcept {
        return std::hash<std::string_view>{}(id.hex());
    }
};

