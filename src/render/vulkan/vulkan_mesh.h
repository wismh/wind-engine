#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::render {

class VulkanMesh final : public IMesh {
public:
    VulkanMesh(std::shared_ptr<VulkanContext> context, const MeshDesc& desc);
    ~VulkanMesh() override;

    VulkanMesh(const VulkanMesh&) = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool ensure_gpu_buffers();
    void draw(VkCommandBuffer cmd);

private:
    void destroy_gpu_buffers();

    std::shared_ptr<VulkanContext> context_;
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
    bool valid_ = false;
};

} // namespace engine::render
