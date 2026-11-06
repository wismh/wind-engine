#pragma once

// docs/tech/features/Assets.md

#include <engine/core/export.h>
#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace engine {

// Little-endian blob a cooked `.shader` file holds: GLSL 330 source for OpenGL and SPIR-V for Vulkan.
// The source tree keeps the XML. `asset_codegen --stage` writes this in its place.
[[nodiscard]] ENGINE_API std::vector<std::uint8_t> encode_shader_blob(const render::ShaderDesc& desc);

// `AssetError::Corrupt` at load is this error. A blob that is not `WSHD` version 1 does not decode.
[[nodiscard]] ENGINE_API std::expected<render::ShaderDesc, std::string> decode_shader_blob(
        std::span<const std::uint8_t> bytes);

}
