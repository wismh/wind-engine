#include "opengl_factory.h"

#include "gl_includes.h"
#include "opengl_mesh.h"
#include "opengl_shader.h"
#include "opengl_texture.h"

#include <engine/render/shader_adapt.h>

#include <memory>
#include <utility>

namespace engine::render {
namespace {

class ContextScope {
public:
    explicit ContextScope(const std::function<void()>& make_current) {
        if (!make_current) {
            return;
        }
        prev_window_ = SDL_GL_GetCurrentWindow();
        prev_context_ = SDL_GL_GetCurrentContext();
        make_current();
        const SDL_GLContext current_context = SDL_GL_GetCurrentContext();
        needs_restore_ = (prev_context_ != nullptr && prev_context_ != current_context && prev_window_ != nullptr);
    }

    ~ContextScope() {
        if (needs_restore_) {
            SDL_GL_MakeCurrent(prev_window_, prev_context_);
        }
    }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    SDL_Window* prev_window_ = nullptr;
    SDL_GLContext prev_context_ = nullptr;
    bool needs_restore_ = false;
};

}

OpenGLFactory::OpenGLFactory(std::function<void()> make_current)
    : make_current_(std::move(make_current)) {}

void OpenGLFactory::set_context_activator(std::function<void()> make_current) {
    make_current_ = std::move(make_current);
}

std::shared_ptr<IMesh> OpenGLFactory::create_mesh(const MeshDesc& desc) {
    const ContextScope scope(make_current_);
    auto mesh = std::make_shared<OpenGLMesh>(desc);
    if (!mesh->valid()) {
        return {};
    }
    return mesh;
}

std::shared_ptr<IShader> OpenGLFactory::create_shader(const ShaderDesc& desc) {
    const ContextScope scope(make_current_);
#if defined(ENGINE_WITH_GLES)
    const ShaderDesc adapted = adapt_shader(desc, ShaderTarget::Glsl300Es);
    auto shader = std::make_shared<OpenGLShader>(adapted.vertex_src, adapted.fragment_src);
#else
    auto shader = std::make_shared<OpenGLShader>(desc.vertex_src, desc.fragment_src);
#endif
    if (!shader->valid()) {
        return {};
    }
    return shader;
}

std::shared_ptr<ITexture> OpenGLFactory::create_texture(const TextureDesc& desc) {
    const ContextScope scope(make_current_);
    auto texture = std::make_shared<OpenGLTexture>(desc);
    if (!texture->valid()) {
        return {};
    }
    return texture;
}

}
