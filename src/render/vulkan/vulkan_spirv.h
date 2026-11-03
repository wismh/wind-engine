#pragma once

// docs/tech/modules/Render.md

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render {

struct SpirvUniformMember {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t matrix_stride = 0;
    std::uint32_t array_stride = 0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t array_elements = 0;
    bool is_row_major = false;
};

struct SpirvReflection {
    std::string vertex_entry = "main";
    std::string fragment_entry = "main";
    bool has_ubo = false;
    std::uint32_t ubo_set = 0;
    std::uint32_t ubo_binding = 0;
    std::uint32_t ubo_size = 0;
    bool has_sampler = false;
    std::uint32_t sampler_set = 0;
    std::uint32_t sampler_binding = 1;
    // The sampler is a texture array (Sampler2DArray / sampler2DArray): its descriptor needs a 2D_ARRAY view.
    bool sampler_arrayed = false;
    std::unordered_map<std::string, SpirvUniformMember> uniforms;

    [[nodiscard]] const SpirvUniformMember* find_uniform(std::string_view name) const noexcept;
};

[[nodiscard]] std::string parse_spirv_entry_point(std::span<const std::uint32_t> spirv);
[[nodiscard]] SpirvReflection reflect_spirv_program(
    std::span<const std::uint32_t> vertex_spirv,
    std::span<const std::uint32_t> fragment_spirv);

} // namespace engine::render
