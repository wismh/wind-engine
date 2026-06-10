#pragma once

#include <engine/render/commands.h>
#include <engine/ui/document.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>

namespace engine::ui {

// One piece of a nine-slice draw: the destination rect and the image pattern that fills it (nvgImagePattern's
// origin and extent: the whole texture, scaled so the piece's source rect lands on `rect`).
struct NineSlicePiece {
    render::Rect rect;
    glm::vec2 pattern_origin{};
    glm::vec2 pattern_extent{};
};

struct NineSlicePieces {
    std::array<NineSlicePiece, 9> pieces{};
    std::size_t count = 0;
};

// image_nine_slice arithmetic for a `texture_size` image: insets clamped to the texture, then to
// `rect`, empty pieces skipped, rows top to bottom, columns left to right.
[[nodiscard]] NineSlicePieces nine_slice_pieces(const render::Rect& rect, const BoxInsets& insets,
        glm::vec2 texture_size) noexcept;

}
