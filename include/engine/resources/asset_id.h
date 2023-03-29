#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace engine {

class AssetId {
public:
    static constexpr std::size_t kHexLength = 32;

    constexpr AssetId() noexcept = default;

    explicit constexpr AssetId(std::string_view hex) {
        if (!is_valid(hex)) {
            throw std::invalid_argument("AssetId must be 32 lowercase hex characters");
        }
        for (std::size_t i = 0; i < kHexLength; ++i) {
            hex_[i] = hex[i];
        }
    }

    [[nodiscard]] static constexpr bool is_valid(std::string_view hex) noexcept {
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

    [[nodiscard]] static std::optional<AssetId> parse(std::string_view hex) {
        if (!is_valid(hex)) {
            return std::nullopt;
        }
        return AssetId{hex};
    }

    [[nodiscard]] constexpr std::string_view hex() const noexcept {
        return std::string_view(hex_.data(), hex_.size());
    }

    constexpr auto operator<=>(const AssetId&) const noexcept = default;
    constexpr bool operator==(const AssetId&) const noexcept = default;

private:
    std::array<char, kHexLength> hex_{};
};

}
