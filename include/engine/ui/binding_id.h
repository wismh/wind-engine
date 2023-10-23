#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace engine::ui {

struct BindingId {
    std::uint32_t value = 0;
    constexpr bool operator==(BindingId other) const noexcept { return value == other.value; }
};

[[nodiscard]] constexpr BindingId intern(std::string_view path) noexcept {
    bool has_content = false;
    for (char ch : path) {
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') {
            has_content = true;
            break;
        }
    }
    if (!has_content) {
        return BindingId{};
    }

    std::uint32_t hash = 2166136261u;
    constexpr std::uint32_t kPrime = 16777619u;
    for (char ch : path) {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
        hash *= kPrime;
    }
    if (hash == 0u) {
        hash ^= 0x80000000u;
    }
    return BindingId{hash};
}

}

template <>
struct std::hash<engine::ui::BindingId> {
    std::size_t operator()(const engine::ui::BindingId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};
