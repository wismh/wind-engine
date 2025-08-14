#include "opengl_canvas.h"

#include "gl_includes.h"
#include "nanovg_painter.h"

#include "render/framebuffer_image.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine::render {
namespace {

bool load_gl_entry_points() {
#if defined(ENGINE_WITH_GLES)
    return true;
#elif __has_include(<glad/glad.h>)
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

bool OpenGLCanvas::init(bool with_ui_painter) {
    destroy_context();
    if (window_ == nullptr || window_->window() == nullptr) {
        return false;
    }

#if defined(ENGINE_WITH_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Mirrors WindowSystem::create()'s SDL_GL_ALPHA_SIZE request for a transparent window's pixel
    // format (already fixed at SDL_CreateWindow time) so this call site's GL attributes don't
    // silently drift from the ones that actually picked the format.
    if (window_->is_transparent()) {
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    }

    context_ = SDL_GL_CreateContext(window_->window());
    if (context_ == nullptr) {
        return false;
    }
    if (!load_gl_entry_points()) {
        destroy_context();
        return false;
    }

    if (!with_ui_painter) {
        return true;
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

bool OpenGLCanvas::load_ui_font(const Font& font) {
    if (ui_painter_ == nullptr) {
        return false;
    }
    return ui_painter_->load_ui_font(font);
}

bool OpenGLCanvas::add_font(AssetId id, const Font& font) {
    if (ui_painter_ == nullptr) {
        return false;
    }
    return ui_painter_->add_font(id, font);
}

bool OpenGLCanvas::add_image(AssetId id, const TextureDesc& desc) {
    if (ui_painter_ == nullptr) {
        return false;
    }
    return ui_painter_->add_image(id, desc);
}

bool OpenGLCanvas::reset_ui_painter() {
    if (ui_painter_ == nullptr) {
        return false;
    }
    make_current();
    return ui_painter_->create();
}

void OpenGLCanvas::make_current() {
    // With 2+ live GL contexts whichever context happens to still be
    // "current" from initialization order would otherwise receive every window's draw calls —
    // silent visual corruption with no build/CI signal (no GPU in engine_tests). Making
    // this window's context current before touching any GL state is what makes draw_all() correct.
    // Hit-test layout also measures text through this painter, so the same MakeCurrent is required
    // before nvgTextBounds (otherwise a secondary window would measure against the wrong atlas).
    if (window_ != nullptr && context_ != nullptr && window_->window() != nullptr) {
        SDL_GL_MakeCurrent(window_->window(), context_);
    }
}

bool OpenGLCanvas::set_vsync(bool on) {
    if (context_ == nullptr) {
        return false;
    }
    make_current();
    if (!on) {
        return SDL_GL_SetSwapInterval(0);
    }
    // Adaptive (-1) swaps a frame that missed its vblank at once instead of waiting for the next one.
    return SDL_GL_SetSwapInterval(-1) || SDL_GL_SetSwapInterval(1);
}

void OpenGLCanvas::draw() {
    render();
    present();
}

void OpenGLCanvas::render() {
    make_current();
    if (window_ != nullptr) {
        const glm::ivec2 size = window_->drawable_size();
        if (size.x > 0 && size.y > 0) {
            glViewport(0, 0, size.x, size.y);
        }
    }
    const float clear_alpha = (window_ != nullptr && window_->is_transparent()) ? 0.0f : 1.0f;
    glClearColor(0.0f, 0.0f, 0.0f, clear_alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (ui_painter_ != nullptr && window_ != nullptr) {
        const glm::ivec2 size = window_->drawable_size();
        ui_painter_->begin_frame(static_cast<float>(size.x), static_cast<float>(size.y));
    }
    // Re-arm the shared backend's UI painter pointer to *this* canvas's painter right before
    // execute() reads it. With 2+ windows each owning its own NanoVgPainter,
    // OpenGLRenderBackend::execute() only ever reads whatever painter is currently set — never
    // something captured earlier — so doing this every frame, right here, is what makes each
    // window's CmdDrawUI commands paint through its own painter instead of whichever window
    // happened to init() last.
    if (auto* gl_backend = dynamic_cast<OpenGLRenderBackend*>(backend_)) {
        gl_backend->set_ui_painter(ui_painter_.get());
    }
    if (backend_ != nullptr && commands_ != nullptr) {
        backend_->execute(*commands_);
    }
    if (ui_painter_ != nullptr) {
        ui_painter_->end_frame();
    }
}

TextureDesc OpenGLCanvas::read_pixels() {
    if (window_ == nullptr || context_ == nullptr || window_->window() == nullptr) {
        return {};
    }
    // A hidden or minimized window's back buffer is not defined to hold what it drew.
    constexpr SDL_WindowFlags kUnseen = SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED;
    if ((SDL_GetWindowFlags(window_->window()) & kUnseen) != 0) {
        return {};
    }
    const glm::ivec2 size = window_->drawable_size();
    if (size.x <= 0 || size.y <= 0) {
        return {};
    }
    make_current();
    GLint read_framebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
    GLint pack_alignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4u);
    glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_framebuffer));
    return framebuffer_image(pixels, size.x, size.y, window_->is_transparent());
}

void OpenGLCanvas::present() {
    if (window_ != nullptr) {
        window_->swap();
    }
}

void OpenGLCanvas::destroy_context() {
    // Only clear the shared backend_'s ui_painter_ if THIS canvas is the one that set it
    // (ui_painter_ != nullptr): backend_ is the one OpenGLRenderBackend shared by every window
    //, so a secondary window's canvas — which never created a painter — would
    // otherwise null out the *primary* window's painter the instant its own init()/destructor
    // called this (init() always calls destroy_context() first, even on a brand-new canvas).
    if (ui_painter_ != nullptr) {
        if (auto* gl_backend = dynamic_cast<OpenGLRenderBackend*>(backend_)) {
            gl_backend->set_ui_painter(nullptr);
        }
        ui_painter_.reset();
    }
    if (context_ == nullptr) {
        return;
    }
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

}
