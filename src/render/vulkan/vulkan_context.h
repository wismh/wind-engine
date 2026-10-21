#pragma once

// docs/tech/modules/Render.md

#include "vulkan_functions.h"

#include <cstdint>
#include <optional>

namespace engine::render {

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]] bool ensure_instance();
    [[nodiscard]] bool ensure_device(VkSurfaceKHR surface = VK_NULL_HANDLE);
    void shutdown();

    [[nodiscard]] bool is_instance_ready() const noexcept { return instance_ != VK_NULL_HANDLE; }
    [[nodiscard]] bool is_device_ready() const noexcept { return device_ != VK_NULL_HANDLE; }

    [[nodiscard]] const VulkanFunctions& fn() const noexcept { return fn_; }
    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    [[nodiscard]] std::uint32_t graphics_family() const noexcept { return graphics_family_; }
    [[nodiscard]] VkCommandPool transient_pool() const noexcept { return transient_pool_; }

    [[nodiscard]] std::optional<std::uint32_t> find_memory_type(
        std::uint32_t type_bits,
        VkMemoryPropertyFlags properties) const noexcept;

    [[nodiscard]] VkFormat find_depth_format() const noexcept;

private:
    VulkanFunctions fn_{};
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = 0;
    VkCommandPool transient_pool_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props_{};
    VkPhysicalDeviceProperties device_props_{};
};

} // namespace engine::render
