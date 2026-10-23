#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphic_factory.h>

#include <expected>
#include <string>
#include <string_view>

namespace engine::render {

// Compiles a single Slang module containing both vertex and fragment entry points into a `ShaderDesc`
// populated with SPIR-V bytecode (`vertex_spirv`, `fragment_spirv`) and OpenGL 3.3 Core GLSL
// (`vertex_src`, `fragment_src`).
[[nodiscard]] std::expected<ShaderDesc, std::string> compile_slang_shader(
        std::string_view slang_source,
        std::string_view vertex_entry = "vertexMain",
        std::string_view fragment_entry = "fragmentMain");

// Compiles separate vertex and fragment Slang sources into a `ShaderDesc`.
[[nodiscard]] std::expected<ShaderDesc, std::string> compile_slang_shader_stages(
        std::string_view vertex_slang,
        std::string_view fragment_slang,
        std::string_view vertex_entry = "vertexMain",
        std::string_view fragment_entry = "fragmentMain");

}
