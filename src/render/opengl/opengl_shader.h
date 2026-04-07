#pragma once

#include "gl_includes.h"

#include <engine/render/graphics.h>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <span>
#include <string>
#include <string_view>

namespace engine::render {

class OpenGLShader final : public IShader {
public:
    OpenGLShader(std::string_view vertex_src, std::string_view fragment_src);
    ~OpenGLShader() override;

    OpenGLShader(const OpenGLShader&) = delete;
    OpenGLShader& operator=(const OpenGLShader&) = delete;

    void use() const;
    // Every setter is a no-op for a uniform the program does not have (or that the compiler removed as unused), so
    // the backend can set the lit uniforms for any shader.
    void set_mat4(std::string_view name, const glm::mat4& value) const;
    void set_mat3(std::string_view name, const glm::mat3& value) const;
    void set_vec4(std::string_view name, const glm::vec4& value) const;
    void set_vec3(std::string_view name, const glm::vec3& value) const;
    // A `vec3 name[N]` array from element 0; at most N values reach the program.
    void set_vec3_array(std::string_view name, std::span<const glm::vec3> values) const;
    void set_vec2(std::string_view name, const glm::vec2& value) const;
    void set_int(std::string_view name, int value) const;
    // The vec4 uniform's current value in the program, or zero when the shader has no such uniform.
    [[nodiscard]] glm::vec4 vec4_value(std::string_view name) const;

    [[nodiscard]] bool valid() const noexcept {
        return program_ != 0;
    }

    [[nodiscard]] int uniform_location(std::string_view name) const;

private:
    unsigned int program_ = 0;
};

}
