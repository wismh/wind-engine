#include "vulkan_functions.h"

namespace engine::render {

namespace {

template <typename Fn>
void resolve_instance(PFN_vkGetInstanceProcAddr loader, VkInstance instance, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(loader(instance, name));
}

template <typename Fn>
void resolve_device(PFN_vkGetDeviceProcAddr loader, VkDevice device, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(loader(device, name));
}

} // namespace

bool VulkanFunctions::load_global() {
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        return false;
    }
    vkGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!vkGetInstanceProcAddr) {
        return false;
    }

    resolve_instance(vkGetInstanceProcAddr, VK_NULL_HANDLE, "vkCreateInstance", vkCreateInstance);
    resolve_instance(
        vkGetInstanceProcAddr,
        VK_NULL_HANDLE,
        "vkEnumerateInstanceExtensionProperties",
        vkEnumerateInstanceExtensionProperties);
    resolve_instance(
        vkGetInstanceProcAddr,
        VK_NULL_HANDLE,
        "vkEnumerateInstanceLayerProperties",
        vkEnumerateInstanceLayerProperties);

    return vkCreateInstance != nullptr && vkEnumerateInstanceExtensionProperties != nullptr;
}

bool VulkanFunctions::load_instance(VkInstance instance) {
    if (!vkGetInstanceProcAddr || instance == VK_NULL_HANDLE) {
        return false;
    }

    resolve_instance(vkGetInstanceProcAddr, instance, "vkDestroyInstance", vkDestroyInstance);
    resolve_instance(vkGetInstanceProcAddr, instance, "vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices);
    resolve_instance(
        vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties", vkGetPhysicalDeviceProperties);
    resolve_instance(vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceFeatures", vkGetPhysicalDeviceFeatures);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceQueueFamilyProperties",
        vkGetPhysicalDeviceQueueFamilyProperties);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceMemoryProperties",
        vkGetPhysicalDeviceMemoryProperties);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceFormatProperties",
        vkGetPhysicalDeviceFormatProperties);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceSurfaceSupportKHR",
        vkGetPhysicalDeviceSurfaceSupportKHR);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceSurfaceFormatsKHR",
        vkGetPhysicalDeviceSurfaceFormatsKHR);
    resolve_instance(
        vkGetInstanceProcAddr,
        instance,
        "vkGetPhysicalDeviceSurfacePresentModesKHR",
        vkGetPhysicalDeviceSurfacePresentModesKHR);
    resolve_instance(vkGetInstanceProcAddr, instance, "vkCreateDevice", vkCreateDevice);
    resolve_instance(vkGetInstanceProcAddr, instance, "vkGetDeviceProcAddr", vkGetDeviceProcAddr);
    resolve_instance(vkGetInstanceProcAddr, instance, "vkDestroySurfaceKHR", vkDestroySurfaceKHR);

    return vkDestroyInstance != nullptr && vkEnumeratePhysicalDevices != nullptr && vkCreateDevice != nullptr &&
           vkGetDeviceProcAddr != nullptr && vkGetPhysicalDeviceSurfaceSupportKHR != nullptr;
}

bool VulkanFunctions::load_device(VkDevice device) {
    if (!vkGetDeviceProcAddr || device == VK_NULL_HANDLE) {
        return false;
    }

    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyDevice", vkDestroyDevice);
    resolve_device(vkGetDeviceProcAddr, device, "vkDeviceWaitIdle", vkDeviceWaitIdle);
    resolve_device(vkGetDeviceProcAddr, device, "vkGetDeviceQueue", vkGetDeviceQueue);
    resolve_device(vkGetDeviceProcAddr, device, "vkQueueSubmit", vkQueueSubmit);
    resolve_device(vkGetDeviceProcAddr, device, "vkQueueWaitIdle", vkQueueWaitIdle);
    resolve_device(vkGetDeviceProcAddr, device, "vkQueuePresentKHR", vkQueuePresentKHR);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateSwapchainKHR", vkCreateSwapchainKHR);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroySwapchainKHR", vkDestroySwapchainKHR);
    resolve_device(vkGetDeviceProcAddr, device, "vkGetSwapchainImagesKHR", vkGetSwapchainImagesKHR);
    resolve_device(vkGetDeviceProcAddr, device, "vkAcquireNextImageKHR", vkAcquireNextImageKHR);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateImage", vkCreateImage);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyImage", vkDestroyImage);
    resolve_device(vkGetDeviceProcAddr, device, "vkGetImageMemoryRequirements", vkGetImageMemoryRequirements);
    resolve_device(vkGetDeviceProcAddr, device, "vkAllocateMemory", vkAllocateMemory);
    resolve_device(vkGetDeviceProcAddr, device, "vkFreeMemory", vkFreeMemory);
    resolve_device(vkGetDeviceProcAddr, device, "vkBindImageMemory", vkBindImageMemory);
    resolve_device(vkGetDeviceProcAddr, device, "vkMapMemory", vkMapMemory);
    resolve_device(vkGetDeviceProcAddr, device, "vkUnmapMemory", vkUnmapMemory);
    resolve_device(vkGetDeviceProcAddr, device, "vkCreateImageView", vkCreateImageView);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyImageView", vkDestroyImageView);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateBuffer", vkCreateBuffer);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyBuffer", vkDestroyBuffer);
    resolve_device(vkGetDeviceProcAddr, device, "vkGetBufferMemoryRequirements", vkGetBufferMemoryRequirements);
    resolve_device(vkGetDeviceProcAddr, device, "vkBindBufferMemory", vkBindBufferMemory);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateRenderPass", vkCreateRenderPass);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyRenderPass", vkDestroyRenderPass);
    resolve_device(vkGetDeviceProcAddr, device, "vkCreateFramebuffer", vkCreateFramebuffer);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyFramebuffer", vkDestroyFramebuffer);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateCommandPool", vkCreateCommandPool);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyCommandPool", vkDestroyCommandPool);
    resolve_device(vkGetDeviceProcAddr, device, "vkResetCommandPool", vkResetCommandPool);
    resolve_device(vkGetDeviceProcAddr, device, "vkAllocateCommandBuffers", vkAllocateCommandBuffers);
    resolve_device(vkGetDeviceProcAddr, device, "vkFreeCommandBuffers", vkFreeCommandBuffers);
    resolve_device(vkGetDeviceProcAddr, device, "vkResetCommandBuffer", vkResetCommandBuffer);
    resolve_device(vkGetDeviceProcAddr, device, "vkBeginCommandBuffer", vkBeginCommandBuffer);
    resolve_device(vkGetDeviceProcAddr, device, "vkEndCommandBuffer", vkEndCommandBuffer);

    resolve_device(vkGetDeviceProcAddr, device, "vkCmdBeginRenderPass", vkCmdBeginRenderPass);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdEndRenderPass", vkCmdEndRenderPass);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdSetViewport", vkCmdSetViewport);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdSetScissor", vkCmdSetScissor);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdClearAttachments", vkCmdClearAttachments);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdPipelineBarrier", vkCmdPipelineBarrier);
    resolve_device(vkGetDeviceProcAddr, device, "vkCmdCopyImageToBuffer", vkCmdCopyImageToBuffer);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateSemaphore", vkCreateSemaphore);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroySemaphore", vkDestroySemaphore);
    resolve_device(vkGetDeviceProcAddr, device, "vkCreateFence", vkCreateFence);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyFence", vkDestroyFence);
    resolve_device(vkGetDeviceProcAddr, device, "vkWaitForFences", vkWaitForFences);
    resolve_device(vkGetDeviceProcAddr, device, "vkResetFences", vkResetFences);

    resolve_device(vkGetDeviceProcAddr, device, "vkCreateShaderModule", vkCreateShaderModule);
    resolve_device(vkGetDeviceProcAddr, device, "vkDestroyShaderModule", vkDestroyShaderModule);

    return vkDestroyDevice != nullptr && vkGetDeviceQueue != nullptr && vkCreateSwapchainKHR != nullptr &&
           vkAcquireNextImageKHR != nullptr && vkQueuePresentKHR != nullptr;
}

} // namespace engine::render
