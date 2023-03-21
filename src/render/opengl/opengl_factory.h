#pragma once

#include <engine/render/graphic_factory.h>

namespace engine::render {

class OpenGLFactory final : public IGraphicFactory {
public:
    std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) override;
    std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) override;
    std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) override;
};

}
