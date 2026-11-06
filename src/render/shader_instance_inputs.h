#pragma once

#include <cctype>
#include <string_view>

namespace engine::render {

// Whole-word match, so `aInstancePos` does not match a longer identifier.
[[nodiscard]] inline bool shader_source_has_identifier(std::string_view source, std::string_view ident) noexcept {
    if (ident.empty()) {
        return false;
    }
    std::size_t pos = 0;
    while (pos < source.size()) {
        const std::size_t found = source.find(ident, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool left_ok = found == 0
                || (!std::isalnum(static_cast<unsigned char>(source[found - 1])) && source[found - 1] != '_');
        const std::size_t end = found + ident.size();
        const bool right_ok = end >= source.size()
                || (!std::isalnum(static_cast<unsigned char>(source[end])) && source[end] != '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = found + 1;
    }
    return false;
}

// The GLSL the OpenGL program is compiled from. True when it names uInstanced and locations 7-9's inputs.
// Location 10 (the array layer) is not required: a sampler2D shader ignores it.
[[nodiscard]] inline bool shader_declares_particle_instance_inputs(std::string_view vertex_src) noexcept {
    return shader_source_has_identifier(vertex_src, "uInstanced")
            && shader_source_has_identifier(vertex_src, "aInstancePos")
            && shader_source_has_identifier(vertex_src, "aInstanceScale")
            && shader_source_has_identifier(vertex_src, "aInstanceColor");
}

} // namespace engine::render
