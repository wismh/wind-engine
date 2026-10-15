#include "render/gl_error_log.h"

#include <algorithm>
#include <format>

namespace engine::render {

std::string_view gl_error_name(unsigned int code) {
    // The values from the GL and GLES headers, so this file needs neither.
    switch (code) {
        case 0x0500:
            return "GL_INVALID_ENUM";
        case 0x0501:
            return "GL_INVALID_VALUE";
        case 0x0502:
            return "GL_INVALID_OPERATION";
        case 0x0503:
            return "GL_STACK_OVERFLOW";
        case 0x0504:
            return "GL_STACK_UNDERFLOW";
        case 0x0505:
            return "GL_OUT_OF_MEMORY";
        case 0x0506:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default:
            return "unknown GL error";
    }
}

std::string GlErrorLog::report(unsigned int code, std::string_view where) {
    const bool seen = std::ranges::any_of(reported_, [&](const std::pair<unsigned int, std::string>& entry) {
        return entry.first == code && entry.second == where;
    });
    if (seen) {
        return {};
    }
    reported_.emplace_back(code, std::string(where));
    return std::format("{} (0x{:04X}) after {}; later ones at the same place are not reported", gl_error_name(code),
            code, where);
}

}
