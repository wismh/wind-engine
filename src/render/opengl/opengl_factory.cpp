#include "opengl_factory.h"

#include "context_scope.h"
#include "gl_includes.h"
#include "opengl_mesh.h"
#include "opengl_shader.h"
#include "opengl_texture.h"

#include <engine/render/shader_adapt.h>

#include <memory>
#include <utility>

namespace engine::render {

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

std::shared_ptr<IDynamicTexture> OpenGLFactory::create_dynamic_texture(const DynamicTextureDesc& desc) {
    const ContextScope scope(make_current_);
    auto texture = std::make_shared<OpenGLTexture>(desc, make_current_);
    if (!texture->valid()) {
        return {};
    }
    return texture;
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
