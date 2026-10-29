#include "vulkan_mesh.h"

#include "render/mesh_check.h"

#include <engine/log.h>

#include <string>
#include <utility>

namespace engine::render {

VulkanMesh::VulkanMesh(std::shared_ptr<VulkanContext> context, const MeshDesc& desc)
    : context_(std::move(context)) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }
    vertices_ = desc.vertices;
    indices_ = desc.indices;
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_buffers();
    }
}

VulkanMesh::~VulkanMesh() {
    destroy_gpu_buffers();
}

bool VulkanMesh::ensure_gpu_buffers() {
    if (!valid_ || !context_ || !context_->is_device_ready()) {
        return false;
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        return true;
    }

    const VkDeviceSize vb_size =
        static_cast<VkDeviceSize>(vertices_.size() * sizeof(MeshVertex));
    if (!context_->upload_host_buffer(
            vb_size,
            vertices_.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertex_buffer_,
            vertex_memory_)) {
        return false;
    }

    if (!indices_.empty()) {
        const VkDeviceSize ib_size =
            static_cast<VkDeviceSize>(indices_.size() * sizeof(std::uint32_t));
        if (!context_->upload_host_buffer(
                ib_size,
                indices_.data(),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                index_buffer_,
                index_memory_)) {
            destroy_gpu_buffers();
            return false;
        }
    }
    return true;
}

void VulkanMesh::draw(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE || !ensure_gpu_buffers()) {
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDeviceSize offset = 0;
    fn.vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, &offset);
    if (index_buffer_ != VK_NULL_HANDLE) {
        fn.vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
        fn.vkCmdDrawIndexed(cmd, static_cast<std::uint32_t>(indices_.size()), 1, 0, 0, 0);
    } else {
        fn.vkCmdDraw(cmd, static_cast<std::uint32_t>(vertices_.size()), 1, 0, 0);
    }
}

void VulkanMesh::destroy_gpu_buffers() {
    if (!context_ || !context_->is_device_ready()) {
        vertex_buffer_ = VK_NULL_HANDLE;
        vertex_memory_ = VK_NULL_HANDLE;
        index_buffer_ = VK_NULL_HANDLE;
        index_memory_ = VK_NULL_HANDLE;
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (index_buffer_ != VK_NULL_HANDLE) {
        fn.vkDestroyBuffer(device, index_buffer_, nullptr);
        index_buffer_ = VK_NULL_HANDLE;
    }
    if (index_memory_ != VK_NULL_HANDLE) {
        fn.vkFreeMemory(device, index_memory_, nullptr);
        index_memory_ = VK_NULL_HANDLE;
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        fn.vkDestroyBuffer(device, vertex_buffer_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (vertex_memory_ != VK_NULL_HANDLE) {
        fn.vkFreeMemory(device, vertex_memory_, nullptr);
        vertex_memory_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::render
