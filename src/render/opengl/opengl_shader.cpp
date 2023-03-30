#include "opengl_shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace engine::render {
namespace {

unsigned int compile_shader(unsigned int type, std::string_view src) {
    const unsigned int shader = glCreateShader(type);
    const char* const c_src = src.data();
    const int length = static_cast<int>(src.size());
    glShaderSource(shader, 1, &c_src, &length);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}

OpenGLShader::OpenGLShader(std::string_view vertex_src, std::string_view fragment_src) {
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

void OpenGLShader::set_vec4(std::string_view name, const glm::vec4& value) const {
    glUniform4fv(uniform_location(name), 1, glm::value_ptr(value));
}

void OpenGLShader::set_int(std::string_view name, int value) const {
    glUniform1i(uniform_location(name), value);
}

}
