#pragma once

#include "gl_includes.h"

#include "render/context_objects.h"

#include <utility>
#include <vector>

namespace engine::render {

// Every GL context the engine made (OpenGLCanvas). Main thread only, like every engine API.
[[nodiscard]] ContextObjects& gl_contexts();
// The key of the context that is current now; 0 when none, or one the engine did not make, is current.
[[nodiscard]] ContextObjects::Key current_gl_context();
// Deletes the VAOs other contexts left for the current one (ContextVao::release).
void flush_current_gl_context();

// One VAO per GL context for an object whose buffers are shared between contexts. A window's context shares the
// primary's buffers, not its VAOs, so a VAO made where the object was loaded binds nothing in another window.
class ContextVao {
public:
    ContextVao() = default;
    ~ContextVao() {
        release();
    }

    ContextVao(const ContextVao&) = delete;
    ContextVao& operator=(const ContextVao&) = delete;

    // The VAO of the current context, made on first use there. `setup` runs once per context with the new VAO bound
    // and sets its attribute layout. 0 when no engine context is current.
    template <class Setup>
    [[nodiscard]] GLuint current(Setup&& setup) {
        const ContextObjects::Key key = current_gl_context();
        if (key == 0) {
            return 0;
        }
        for (const Entry& entry : vaos_) {
            if (entry.key == key) {
                return entry.vao;
            }
        }
        // A destroyed context took its VAOs with it.
        std::erase_if(vaos_, [](const Entry& entry) { return !gl_contexts().is_live(entry.key); });
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        if (vao == 0) {
            return 0;
        }
        glBindVertexArray(vao);
        std::forward<Setup>(setup)();
        glBindVertexArray(0);
        vaos_.push_back(Entry{.key = key, .vao = vao});
        return vao;
    }

    // Deletes the current context's VAO now and leaves the others to their context's next flush.
    void release();

private:
    struct Entry {
        ContextObjects::Key key = 0;
        GLuint vao = 0;
    };

    std::vector<Entry> vaos_;
};

}
