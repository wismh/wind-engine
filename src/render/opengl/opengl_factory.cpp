#include "opengl_factory.h"

#include "opengl_mesh.h"
#include "opengl_shader.h"
#include "opengl_texture.h"

#include <engine/render/shader_adapt.h>

#include <memory>

namespace engine::render {

std::shared_ptr<IMesh> OpenGLFactory::create_mesh(const MeshDesc& desc) {
    auto mesh = std::make_shared<OpenGLMesh>(desc);
    if (!mesh->valid()) {
        return {};
    }
    return mesh;
}

std::shared_ptr<IShader> OpenGLFactory::create_shader(const ShaderDesc& desc) {
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
    auto texture = std::make_shared<OpenGLTexture>(desc);
    if (!texture->valid()) {
        return {};
    }
    return texture;
}

}
