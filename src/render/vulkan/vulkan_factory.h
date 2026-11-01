#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"

#include <engine/render/graphic_factory.h>

#include <memory>

namespace engine::render {

class VulkanFactory final : public IGraphicFactory {
public:
    explicit VulkanFactory(std::shared_ptr<VulkanContext> context);
    ~VulkanFactory() override = default;

    VulkanFactory(const VulkanFactory&) = delete;
    VulkanFactory& operator=(const VulkanFactory&) = delete;

    std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) override;
    std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) override;
    std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) override;
    std::shared_ptr<IDynamicTexture> create_dynamic_texture(const DynamicTextureDesc& desc) override;

private:
    std::shared_ptr<VulkanContext> context_;
};

} // namespace engine::render
