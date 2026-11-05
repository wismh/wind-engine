#include "vulkan_mesh.h"

#include "render/mesh_check.h"

#include <engine/log.h>

#include <string>
#include <utility>

namespace engine::render {

VulkanMesh::VulkanMesh(std::shared_ptr<VulkanContext> context, const MeshDesc& desc)
    : IMesh(desc)
    , context_(std::move(context)) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }
    vertices_ = desc.vertices;
    indices_ = desc.indices;
    influences_ = desc.influences;
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_buffers();
    }
}

VulkanMesh::~VulkanMesh() {
    retire_gpu_buffers();
}

bool VulkanMesh::replace(const MeshDesc& desc) {
    if (!valid_) {
        return false;
    }
    if (!context_ || !context_->is_device_ready() || vertex_buffer_ == VK_NULL_HANDLE) {
        // Nothing on the GPU yet: the next ensure_gpu_buffers uploads the new data.
        vertices_ = desc.vertices;
        indices_ = desc.indices;
        influences_ = desc.influences;
        return true;
    }

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    if (!context_->upload_host_buffer(static_cast<VkDeviceSize>(desc.vertices.size() * sizeof(MeshVertex)),
            desc.vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer, vertex_memory)) {
        return false;
    }
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    if (!desc.indices.empty() &&
            !context_->upload_host_buffer(static_cast<VkDeviceSize>(desc.indices.size() * sizeof(std::uint32_t)),
                    desc.indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer, index_memory)) {
        // Never submitted: free it now.
        const VulkanFunctions& fn = context_->fn();
        fn.vkDestroyBuffer(context_->device(), vertex_buffer, nullptr);
        fn.vkFreeMemory(context_->device(), vertex_memory, nullptr);
        return false;
    }
    VkBuffer skin_buffer = VK_NULL_HANDLE;
    VkDeviceMemory skin_memory = VK_NULL_HANDLE;
    if (!desc.influences.empty() &&
            !context_->upload_host_buffer(static_cast<VkDeviceSize>(desc.influences.size() * sizeof(SkinInfluence)),
                    desc.influences.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, skin_buffer, skin_memory)) {
        const VulkanFunctions& fn = context_->fn();
        fn.vkDestroyBuffer(context_->device(), vertex_buffer, nullptr);
        fn.vkFreeMemory(context_->device(), vertex_memory, nullptr);
        if (index_buffer != VK_NULL_HANDLE) {
            fn.vkDestroyBuffer(context_->device(), index_buffer, nullptr);
            fn.vkFreeMemory(context_->device(), index_memory, nullptr);
        }
        return false;
    }

    retire_gpu_buffers();
    vertex_buffer_ = vertex_buffer;
    vertex_memory_ = vertex_memory;
    index_buffer_ = index_buffer;
    index_memory_ = index_memory;
    skin_buffer_ = skin_buffer;
    skin_memory_ = skin_memory;
    vertices_ = desc.vertices;
    indices_ = desc.indices;
    influences_ = desc.influences;
    return true;
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
            retire_gpu_buffers();
            return false;
        }
    }
    if (!influences_.empty()) {
        const VkDeviceSize skin_size =
            static_cast<VkDeviceSize>(influences_.size() * sizeof(SkinInfluence));
        if (!context_->upload_host_buffer(
                skin_size,
                influences_.data(),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                skin_buffer_,
                skin_memory_)) {
            retire_gpu_buffers();
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
    if (skin_buffer_ != VK_NULL_HANDLE) {
        fn.vkCmdBindVertexBuffers(cmd, 1, 1, &skin_buffer_, &offset);
    }
    if (index_buffer_ != VK_NULL_HANDLE) {
        fn.vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
        fn.vkCmdDrawIndexed(cmd, static_cast<std::uint32_t>(indices_.size()), 1, 0, 0, 0);
    } else {
        fn.vkCmdDraw(cmd, static_cast<std::uint32_t>(vertices_.size()), 1, 0, 0);
    }
}

void VulkanMesh::retire_gpu_buffers() {
    if (context_) {
        context_->retire_buffer(skin_buffer_, skin_memory_);
        context_->retire_buffer(index_buffer_, index_memory_);
        context_->retire_buffer(vertex_buffer_, vertex_memory_);
    }
    vertex_buffer_ = VK_NULL_HANDLE;
    vertex_memory_ = VK_NULL_HANDLE;
    index_buffer_ = VK_NULL_HANDLE;
    index_memory_ = VK_NULL_HANDLE;
    skin_buffer_ = VK_NULL_HANDLE;
    skin_memory_ = VK_NULL_HANDLE;
}

} // namespace engine::render
