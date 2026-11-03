#include "vulkan_context.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace engine::render {

namespace {

bool has_layer(const VulkanFunctions& fn, const char* layer_name) {
    if (!fn.vkEnumerateInstanceLayerProperties) {
        return false;
    }
    std::uint32_t count = 0;
    if (fn.vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (fn.vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, layer_name) == 0) {
            return true;
        }
    }
    return false;
}

bool wants_validation_layers() {
    const char* env = std::getenv("WIND_VK_VALIDATION");
    return env != nullptr && std::strcmp(env, "1") == 0;
}

int score_device_type(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 1;
    default:
        return 0;
    }
}

std::optional<std::uint32_t> find_queue_family(
    const VulkanFunctions& fn,
    VkPhysicalDevice device,
    VkSurfaceKHR surface) {
    std::uint32_t family_count = 0;
    fn.vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
    if (family_count == 0) {
        return std::nullopt;
    }
    std::vector<VkQueueFamilyProperties> families(family_count);
    fn.vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

    for (std::uint32_t i = 0; i < family_count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            continue;
        }
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present_support = VK_FALSE;
            if (fn.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support) != VK_SUCCESS ||
                present_support == VK_FALSE) {
                continue;
            }
        }
        return i;
    }
    return std::nullopt;
}

} // namespace

VulkanContext::~VulkanContext() {
    shutdown();
}

bool VulkanContext::ensure_instance() {
    if (instance_ != VK_NULL_HANDLE) {
        return true;
    }
    if (!fn_.load_global()) {
        return false;
    }

    Uint32 sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    std::vector<const char*> extensions;
    if (sdl_exts != nullptr && sdl_ext_count > 0) {
        extensions.assign(sdl_exts, sdl_exts + sdl_ext_count);
    }

    std::vector<const char*> layers;
    constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    if (wants_validation_layers() && has_layer(fn_, kValidationLayer)) {
        layers.push_back(kValidationLayer);
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Wind Engine";
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app_info.pEngineName = "Wind";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    create_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    if (fn_.vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS || instance_ == VK_NULL_HANDLE) {
        instance_ = VK_NULL_HANDLE;
        return false;
    }

    if (!fn_.load_instance(instance_)) {
        if (fn_.vkDestroyInstance) {
            fn_.vkDestroyInstance(instance_, nullptr);
        }
        instance_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanContext::ensure_device(VkSurfaceKHR surface) {
    if (!ensure_instance()) {
        return false;
    }
    if (device_ != VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present_support = VK_FALSE;
            if (fn_.vkGetPhysicalDeviceSurfaceSupportKHR(
                    physical_device_, graphics_family_, surface, &present_support) != VK_SUCCESS ||
                present_support == VK_FALSE) {
                return false;
            }
        }
        return true;
    }

    std::uint32_t device_count = 0;
    if (fn_.vkEnumeratePhysicalDevices(instance_, &device_count, nullptr) != VK_SUCCESS || device_count == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    if (fn_.vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()) != VK_SUCCESS) {
        return false;
    }

    int best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    std::uint32_t best_family = 0;

    for (VkPhysicalDevice candidate : devices) {
        const auto family = find_queue_family(fn_, candidate, surface);
        if (!family.has_value()) {
            continue;
        }
        VkPhysicalDeviceProperties props{};
        fn_.vkGetPhysicalDeviceProperties(candidate, &props);
        const int score = score_device_type(props.deviceType);
        if (score > best_score) {
            best_score = score;
            best_device = candidate;
            best_family = *family;
        }
    }

    if (best_device == VK_NULL_HANDLE) {
        return false;
    }

    physical_device_ = best_device;
    graphics_family_ = best_family;
    fn_.vkGetPhysicalDeviceProperties(physical_device_, &device_props_);
    fn_.vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props_);

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    std::vector<const char*> device_extensions;
    if (surface != VK_NULL_HANDLE) {
        device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkPhysicalDeviceFeatures enabled_features{};

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.empty() ? nullptr : device_extensions.data();
    device_info.pEnabledFeatures = &enabled_features;

    if (fn_.vkCreateDevice(physical_device_, &device_info, nullptr, &device_) != VK_SUCCESS ||
        device_ == VK_NULL_HANDLE) {
        device_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
        return false;
    }

    if (!fn_.load_device(device_)) {
        if (fn_.vkDestroyDevice) {
            fn_.vkDestroyDevice(device_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
        return false;
    }

    fn_.vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = graphics_family_;
    if (fn_.vkCreateCommandPool(device_, &pool_info, nullptr, &transient_pool_) != VK_SUCCESS) {
        transient_pool_ = VK_NULL_HANDLE;
    }

    return true;
}

void VulkanContext::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        if (fn_.vkDeviceWaitIdle) {
            fn_.vkDeviceWaitIdle(device_);
        }
        release_retired();
        if (transient_pool_ != VK_NULL_HANDLE && fn_.vkDestroyCommandPool) {
            fn_.vkDestroyCommandPool(device_, transient_pool_, nullptr);
            transient_pool_ = VK_NULL_HANDLE;
        }
        if (fn_.vkDestroyDevice) {
            fn_.vkDestroyDevice(device_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        graphics_queue_ = VK_NULL_HANDLE;
    }
    physical_device_ = VK_NULL_HANDLE;
    if (instance_ != VK_NULL_HANDLE) {
        if (fn_.vkDestroyInstance) {
            fn_.vkDestroyInstance(instance_, nullptr);
        }
        instance_ = VK_NULL_HANDLE;
    }
}

std::optional<std::uint32_t> VulkanContext::find_memory_type(
    std::uint32_t type_bits,
    VkMemoryPropertyFlags properties) const noexcept {
    for (std::uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) != 0 &&
            (mem_props_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

VkFormat VulkanContext::find_depth_format() const noexcept {
    if (physical_device_ == VK_NULL_HANDLE || !fn_.vkGetPhysicalDeviceFormatProperties) {
        return VK_FORMAT_D32_SFLOAT;
    }
    constexpr VkFormat kCandidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat fmt : kCandidates) {
        VkFormatProperties props{};
        fn_.vkGetPhysicalDeviceFormatProperties(physical_device_, fmt, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return fmt;
        }
    }
    return VK_FORMAT_D32_SFLOAT;
}

bool VulkanContext::create_buffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& out_buffer,
    VkDeviceMemory& out_memory) const {
    out_buffer = VK_NULL_HANDLE;
    out_memory = VK_NULL_HANDLE;
    if (device_ == VK_NULL_HANDLE || size == 0 || !fn_.vkCreateBuffer) {
        return false;
    }

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = size;
    buf_info.usage = usage;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (fn_.vkCreateBuffer(device_, &buf_info, nullptr, &out_buffer) != VK_SUCCESS) {
        out_buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements reqs{};
    fn_.vkGetBufferMemoryRequirements(device_, out_buffer, &reqs);
    const auto mem_type = find_memory_type(reqs.memoryTypeBits, properties);
    if (!mem_type.has_value()) {
        fn_.vkDestroyBuffer(device_, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = *mem_type;
    if (fn_.vkAllocateMemory(device_, &alloc_info, nullptr, &out_memory) != VK_SUCCESS) {
        fn_.vkDestroyBuffer(device_, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        out_memory = VK_NULL_HANDLE;
        return false;
    }

    if (fn_.vkBindBufferMemory(device_, out_buffer, out_memory, 0) != VK_SUCCESS) {
        fn_.vkFreeMemory(device_, out_memory, nullptr);
        fn_.vkDestroyBuffer(device_, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        out_memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanContext::upload_host_buffer(
    VkDeviceSize size,
    const void* data,
    VkBufferUsageFlags usage,
    VkBuffer& out_buffer,
    VkDeviceMemory& out_memory) const {
    if (!create_buffer(
            size,
            usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            out_buffer,
            out_memory)) {
        return false;
    }
    if (data != nullptr) {
        void* mapped = nullptr;
        if (fn_.vkMapMemory(device_, out_memory, 0, size, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
            fn_.vkFreeMemory(device_, out_memory, nullptr);
            fn_.vkDestroyBuffer(device_, out_buffer, nullptr);
            out_buffer = VK_NULL_HANDLE;
            out_memory = VK_NULL_HANDLE;
            return false;
        }
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        fn_.vkUnmapMemory(device_, out_memory);
    }
    return true;
}

VkCommandBuffer VulkanContext::begin_single_time_commands() const {
    if (device_ == VK_NULL_HANDLE || transient_pool_ == VK_NULL_HANDLE || !fn_.vkAllocateCommandBuffers) {
        return VK_NULL_HANDLE;
    }
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = transient_pool_;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (fn_.vkAllocateCommandBuffers(device_, &alloc_info, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (fn_.vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        fn_.vkFreeCommandBuffers(device_, transient_pool_, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

void VulkanContext::end_single_time_commands(VkCommandBuffer cmd) const {
    if (cmd == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE || transient_pool_ == VK_NULL_HANDLE) {
        return;
    }
    fn_.vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    fn_.vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    fn_.vkQueueWaitIdle(graphics_queue_);
    fn_.vkFreeCommandBuffers(device_, transient_pool_, 1, &cmd);
}

void VulkanContext::retire_buffer(VkBuffer buffer, VkDeviceMemory memory) {
    if (device_ == VK_NULL_HANDLE || (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)) {
        return;
    }
    retired_.retire(RetiredBuffer{.buffer = buffer, .memory = memory});
}

std::uint64_t VulkanContext::note_submitted() {
    return retired_.submitted();
}

void VulkanContext::note_completed(std::uint64_t serial) {
    for (const RetiredBuffer& retired : retired_.completed(serial)) {
        destroy_retired(retired);
    }
}

void VulkanContext::release_retired() {
    for (const RetiredBuffer& retired : retired_.take_all()) {
        destroy_retired(retired);
    }
}

void VulkanContext::destroy_retired(const RetiredBuffer& retired) const {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (retired.buffer != VK_NULL_HANDLE && fn_.vkDestroyBuffer) {
        fn_.vkDestroyBuffer(device_, retired.buffer, nullptr);
    }
    if (retired.memory != VK_NULL_HANDLE && fn_.vkFreeMemory) {
        fn_.vkFreeMemory(device_, retired.memory, nullptr);
    }
}

} // namespace engine::render
