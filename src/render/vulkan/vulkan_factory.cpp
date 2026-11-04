#include "vulkan_factory.h"

#include "vulkan_mesh.h"
#include "vulkan_shader.h"
#include "vulkan_texture.h"

#include <memory>
#include <utility>

namespace engine::render {

VulkanFactory::VulkanFactory(std::shared_ptr<VulkanContext> context)
    : context_(std::move(context)) {}

std::shared_ptr<IMesh> VulkanFactory::create_mesh(const MeshDesc& desc) {
    auto mesh = std::make_shared<VulkanMesh>(context_, desc);
    if (!mesh->valid()) {
        return {};
    }
    return mesh;
}

std::shared_ptr<IShader> VulkanFactory::create_shader(const ShaderDesc& desc) {
    auto shader = std::make_shared<VulkanShader>(context_, desc);
    if (!shader->valid()) {
        return {};
    }
    return shader;
}

std::shared_ptr<ITexture> VulkanFactory::create_texture(const TextureDesc& desc) {
    auto texture = std::make_shared<VulkanTexture>(context_, desc);
    if (!texture->valid()) {
        return {};
    }
    return texture;
}

std::shared_ptr<ITexture> VulkanFactory::create_texture_array(const TextureArrayDesc& desc) {
    auto texture = std::make_shared<VulkanTexture>(context_, desc);
    if (!texture->valid()) {
        return {};
    }
    return texture;
}

std::shared_ptr<IDynamicTexture> VulkanFactory::create_dynamic_texture(const DynamicTextureDesc& desc) {
    auto texture = std::make_shared<VulkanTexture>(context_, desc);
    if (!texture->valid()) {
        return {};
    }
    return texture;
}

} // namespace engine::render
