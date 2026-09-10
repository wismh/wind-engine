#pragma once

#include <engine/render/graphic_factory.h>

#include <functional>

namespace engine::render {

class OpenGLFactory final : public IGraphicFactory {
public:
    OpenGLFactory() = default;
    explicit OpenGLFactory(std::function<void()> make_current);

    void set_context_activator(std::function<void()> make_current);

    std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) override;
    std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) override;
    std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) override;
    std::shared_ptr<IDynamicTexture> create_dynamic_texture(const DynamicTextureDesc& desc) override;

private:
    std::function<void()> make_current_;
};

}
