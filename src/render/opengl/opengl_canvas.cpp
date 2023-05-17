#include "opengl_canvas.h"

#include "gl_includes.h"
#include "opengl_runtime.h"

namespace engine::render {
namespace {

bool load_gl_entry_points() {
#if __has_include(<glad/glad.h>)
    return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) != 0;
#else
    return gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) != 0;
#endif
}

}

OpenGLCanvas::OpenGLCanvas(WindowSystem& window, CommandBuffer& commands, IRenderBackend& backend)
    : window_(&window)
    , commands_(&commands)
    , backend_(&backend) {}

OpenGLCanvas::~OpenGLCanvas() {
    destroy_context();
}

bool OpenGLCanvas::init() {
    destroy_context();
    if (window_ == nullptr || window_->window() == nullptr) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    context_ = SDL_GL_CreateContext(window_->window());
    if (context_ == nullptr) {
        return false;
    }
    if (!load_gl_entry_points()) {
        destroy_context();
        return false;
    }
    return true;
}

void OpenGLCanvas::Draw() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (backend_ != nullptr && commands_ != nullptr) {
        backend_->execute(*commands_);
    }
    if (window_ != nullptr) {
        window_->swap();
    }
}

void OpenGLCanvas::destroy_context() {
    if (context_ == nullptr) {
        return;
    }
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

}
