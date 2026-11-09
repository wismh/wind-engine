#pragma once

// Private. Standard base64 (RFC 4648, `+` `/`, `=` padding). Not installed.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

[[nodiscard]] std::string base64_encode(std::span<const std::uint8_t> bytes);
// nullopt for a length that is not a multiple of 4, a character outside the alphabet, or padding before the end.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text);

}
