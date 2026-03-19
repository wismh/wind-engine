#include "context_vao.h"

namespace engine::render {

ContextObjects& gl_contexts() {
    static ContextObjects contexts;
    return contexts;
}

ContextObjects::Key current_gl_context() {
    return gl_contexts().key(SDL_GL_GetCurrentContext());
}

void flush_current_gl_context() {
    const std::vector<unsigned int> vaos = gl_contexts().take_deferred(current_gl_context());
    if (!vaos.empty()) {
        glDeleteVertexArrays(static_cast<GLsizei>(vaos.size()), vaos.data());
    }
}

void ContextVao::release() {
    const ContextObjects::Key current = current_gl_context();
    for (const Entry& entry : vaos_) {
        if (entry.key == current) {
            glDeleteVertexArrays(1, &entry.vao);
        } else {
            gl_contexts().defer_delete(entry.key, entry.vao);
        }
    }
    vaos_.clear();
}

}
