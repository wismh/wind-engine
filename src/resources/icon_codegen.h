#pragma once

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class IconCodegenErrorKind {
    Io,
    Decode,
    NotSquare,
    TooSmall,
};

struct IconCodegenError {
    IconCodegenErrorKind kind = IconCodegenErrorKind::Io;
    std::string message;
};

// Below this the largest platform target (macOS ic10, 1024x1024) would need upscaling.
inline constexpr int kIconCodegenMinSize = 1024;

// Square RGBA8 downsample of `src` to `size` x `size` (linear filter, stb_image_resize2).
[[nodiscard]] render::TextureDesc icon_resize_rgba(const render::TextureDesc& src, int size);

// In-memory PNG encode of an RGBA8 image. Empty on encode failure.
[[nodiscard]] std::vector<std::uint8_t> icon_encode_png(const render::TextureDesc& image);

// Windows ICO container: resizes `master` to each of `sizes` and embeds each as a PNG blob
// (Vista+ accepts PNG-encoded ICONDIRENTRY payloads, so no BMP/DIB encoding is needed).
[[nodiscard]] std::vector<std::uint8_t> icon_encode_ico(const render::TextureDesc& master, std::span<const int> sizes);

// macOS ICNS container spanning the standard plain-PNG OSType slots (16 up to 1024).
[[nodiscard]] std::vector<std::uint8_t> icon_encode_icns(const render::TextureDesc& master);

// Validates `input_png_path` (square, >= kIconCodegenMinSize) and emits icon.ico, icon.icns,
// the Android mipmap-*/ic_launcher.png set, and favicon.png under `output_dir`.
[[nodiscard]] std::expected<void, IconCodegenError> icon_codegen_write(
        std::string_view input_png_path, std::string_view output_dir);

}
