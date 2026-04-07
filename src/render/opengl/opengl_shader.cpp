#include "opengl_shader.h"

#include <engine/log.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <format>
#include <string>

namespace engine::render {
namespace {

// The driver's log for a shader or a program, without its trailing newline. "no log" when the driver wrote none.
template <class GetLength, class GetLog>
std::string info_log(unsigned int object, GetLength get_length, GetLog get_log) {
    int length = 0;
    get_length(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return "no log";
    }
    std::string text(static_cast<std::size_t>(length), char{});
    int written = 0;
    get_log(object, length, &written, text.data());
    text.resize(static_cast<std::size_t>(written));
    text.erase(text.find_last_not_of("\r\n") + 1);
    return text;
}

std::string shader_info_log(unsigned int shader) {
    return info_log(shader, glGetShaderiv, glGetShaderInfoLog);
}

std::string program_info_log(unsigned int program) {
    return info_log(program, glGetProgramiv, glGetProgramInfoLog);
}

unsigned int compile_shader(unsigned int type, std::string_view src) {
    const unsigned int shader = glCreateShader(type);
    const char* const c_src = src.data();
    const int length = static_cast<int>(src.size());
    glShaderSource(shader, 1, &c_src, &length);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0) {
        log::error(std::format("{} shader failed to compile: {}", type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                shader_info_log(shader)));
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}

OpenGLShader::OpenGLShader(std::string_view vertex_src, std::string_view fragment_src) {
    if (vertex_src.empty() || fragment_src.empty()) {
        return;
    }
    const unsigned int vertex = compile_shader(GL_VERTEX_SHADER, vertex_src);
    const unsigned int fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        return;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    int success = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (success == 0) {
        log::error(std::format("shader program failed to link: {}", program_info_log(program_)));
        glDeleteProgram(program_);
        program_ = 0;
    }
}

OpenGLShader::~OpenGLShader() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

void OpenGLShader::use() const {
    glUseProgram(program_);
}

int OpenGLShader::uniform_location(std::string_view name) const {
    const std::string name_z(name);
    return glGetUniformLocation(program_, name_z.c_str());
}

void OpenGLShader::set_mat4(std::string_view name, const glm::mat4& value) const {
    glUniformMatrix4fv(uniform_location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void OpenGLShader::set_mat3(std::string_view name, const glm::mat3& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }
}

void OpenGLShader::set_vec4(std::string_view name, const glm::vec4& value) const {
    glUniform4fv(uniform_location(name), 1, glm::value_ptr(value));
}

void OpenGLShader::set_vec3(std::string_view name, const glm::vec3& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform3fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::set_vec3_array(std::string_view name, std::span<const glm::vec3> values) const {
    const int location = uniform_location(name);
    if (location >= 0 && !values.empty()) {
        glUniform3fv(location, static_cast<int>(values.size()), glm::value_ptr(values.front()));
    }
}

glm::vec4 OpenGLShader::vec4_value(std::string_view name) const {
    glm::vec4 value{0.0f};
    const int location = uniform_location(name);
    if (location >= 0) {
        glGetUniformfv(program_, location, glm::value_ptr(value));
    }
    return value;
}

void OpenGLShader::set_vec2(std::string_view name, const glm::vec2& value) const {
    glUniform2fv(uniform_location(name), 1, glm::value_ptr(value));
}

void OpenGLShader::set_int(std::string_view name, int value) const {
    glUniform1i(uniform_location(name), value);
}

}
