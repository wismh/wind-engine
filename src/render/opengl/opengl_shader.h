#pragma once

#include "gl_includes.h"

#include <engine/render/graphics.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

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
    void set_mat4(std::string_view name, const glm::mat4& value) const;
    void set_vec4(std::string_view name, const glm::vec4& value) const;
    void set_int(std::string_view name, int value) const;

    [[nodiscard]] bool valid() const noexcept {
        return program_ != 0;
    }

private:
    [[nodiscard]] int uniform_location(std::string_view name) const;

    unsigned int program_ = 0;
};

}
