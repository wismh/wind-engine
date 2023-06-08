#include "opengl_canvas.h"

#include "gl_includes.h"
#include "nanovg_painter.h"
#include "opengl_runtime.h"

#include <memory>

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

    ui_painter_ = std::make_unique<NanoVgPainter>();
    if (!ui_painter_->create()) {
        ui_painter_.reset();
        destroy_context();
        return false;
    }
    if (auto* gl_backend = dynamic_cast<OpenGLRenderBackend*>(backend_)) {
        gl_backend->set_ui_painter(ui_painter_.get());
    }
    return true;
}

void OpenGLCanvas::Draw() {
    if (window_ != nullptr) {
        const glm::ivec2 size = window_->drawable_size();
        if (size.x > 0 && size.y > 0) {
            glViewport(0, 0, size.x, size.y);
        }
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (ui_painter_ != nullptr && window_ != nullptr) {
        const glm::ivec2 size = window_->drawable_size();
        ui_painter_->begin_frame(static_cast<float>(size.x), static_cast<float>(size.y));
    }
    if (backend_ != nullptr && commands_ != nullptr) {
        backend_->execute(*commands_);
    }
    if (ui_painter_ != nullptr) {
        ui_painter_->end_frame();
    }
    if (window_ != nullptr) {
        window_->swap();
    }
}

void OpenGLCanvas::destroy_context() {
    if (auto* gl_backend = dynamic_cast<OpenGLRenderBackend*>(backend_)) {
        gl_backend->set_ui_painter(nullptr);
    }
    ui_painter_.reset();
    if (context_ == nullptr) {
        return;
    }
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

}
