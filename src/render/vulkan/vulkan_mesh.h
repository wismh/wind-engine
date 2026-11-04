#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"

#include <engine/render/graphic_factory.h>

#include <cstddef>
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

    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices_.size(); }
    [[nodiscard]] std::size_t index_count() const noexcept { return indices_.size(); }

protected:
    // New buffers for `desc`; the old ones are retired (`VulkanContext::retire_buffer`), never written while a frame in
    // flight may read them. A failed upload keeps the old buffers and contents.
    bool replace(const MeshDesc& desc) override;

private:
    // Hands the buffers to the context's retire queue, so a frame in flight that reads them keeps them.
    void retire_gpu_buffers();

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
