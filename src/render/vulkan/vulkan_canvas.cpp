#include "vulkan_canvas.h"

#include "render/framebuffer_image.h"

#include <engine/log.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <string>

namespace engine::render {

namespace {

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& fmt : formats) {
        if ((fmt.format == VK_FORMAT_B8G8R8A8_UNORM || fmt.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return formats.front();
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (!vsync) {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, bool transparent) {
    if (transparent) {
        if ((supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        if ((supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
    }
    if ((supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

void log_vk_failure(const char* call, VkResult result) {
    log::error(std::format(
            "Vulkan: {} failed ({}, {})",
            call,
            vk_result_name(result),
            static_cast<int>(result)));
}

} // namespace

VulkanCanvas::VulkanCanvas(WindowSystem& window, CommandBuffer& commands, VulkanRenderBackend& backend)
    : window_(&window)
    , commands_(&commands)
    , backend_(&backend) {}

VulkanCanvas::~VulkanCanvas() {
    shutdown();
}

SDL_WindowFlags VulkanCanvas::prepare_window(const WindowDesc&) {
    return SDL_WINDOW_VULKAN;
}

bool VulkanCanvas::init(bool with_ui_painter) {
    shutdown();
    if (window_ == nullptr || window_->window() == nullptr || backend_ == nullptr) {
        log::error("Vulkan: canvas init has no window");
        return false;
    }

    VulkanContext& ctx = backend_->context();
    if (!ctx.ensure_instance()) {
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(window_->window(), ctx.instance(), nullptr, &surface_) ||
        surface_ == VK_NULL_HANDLE) {
        log::error(std::string("Vulkan: SDL_Vulkan_CreateSurface failed (") + SDL_GetError() + ")");
        surface_ = VK_NULL_HANDLE;
        return false;
    }
    if (!ctx.ensure_device(surface_)) {
        shutdown();
        return false;
    }
    if (!create_sync_and_commands()) {
        log::error("Vulkan: frame sync objects were not created");
        shutdown();
        return false;
    }
    if (!recreate_swapchain()) {
        log::error("Vulkan: the swapchain was not created");
        shutdown();
        return false;
    }

    if (with_ui_painter) {
        ui_device_ = std::make_unique<VulkanUiDevice>(backend_->context_ptr());
        if (!ui_device_->create()) {
            log::error("the Vulkan UI renderer's shader did not build; this window draws no UI");
            ui_device_.reset();
            shutdown();
            return false;
        }
        ui_renderer_ = std::make_unique<ui::UiRenderer>(backend_->ui_resources(), *ui_device_);
        backend_->set_ui_painter(ui_painter());
    }
    return true;
}

void VulkanCanvas::shutdown() {
    if (backend_ != nullptr && ui_painter() != nullptr) {
        backend_->set_ui_painter(nullptr);
    }
    ui_renderer_.reset();
    ui_device_.reset();

    if (backend_ != nullptr && backend_->context().is_device_ready()) {
        const VulkanFunctions& fn = backend_->context().fn();
        if (fn.vkDeviceWaitIdle) {
            fn.vkDeviceWaitIdle(backend_->context().device());
            backend_->context().release_retired();
        }
    }

    destroy_swapchain();
    destroy_sync_and_commands();

    if (surface_ != VK_NULL_HANDLE && backend_ != nullptr && backend_->context().is_instance_ready()) {
        const VulkanFunctions& fn = backend_->context().fn();
        if (fn.vkDestroySurfaceKHR) {
            fn.vkDestroySurfaceKHR(backend_->context().instance(), surface_, nullptr);
        }
        surface_ = VK_NULL_HANDLE;
    }
    frame_rendered_ = false;
}

bool VulkanCanvas::load_ui_font(const Font& font) {
    return ui_renderer_ != nullptr && ui_renderer_->load_ui_font(font);
}

bool VulkanCanvas::add_font(AssetId id, const Font& font) {
    return ui_renderer_ != nullptr && ui_renderer_->add_font(id, font);
}

bool VulkanCanvas::add_image(AssetId id, const TextureDesc& desc) {
    return ui_renderer_ != nullptr && ui_renderer_->add_image(id, desc);
}

bool VulkanCanvas::reset_ui_painter() {
    if (ui_renderer_ == nullptr) {
        return false;
    }
    ui_renderer_->drop_images();
    return true;
}

void VulkanCanvas::make_current() {
    if (backend_ != nullptr && ui_renderer_ != nullptr) {
        backend_->set_ui_painter(ui_painter());
    }
}

bool VulkanCanvas::set_vsync(bool on) {
    if (surface_ == VK_NULL_HANDLE || backend_ == nullptr || !backend_->context().is_device_ready()) {
        return false;
    }
    if (vsync_ != on) {
        vsync_ = on;
        swapchain_dirty_ = true;
    }
    return true;
}

void VulkanCanvas::draw() {
    render();
    present();
}

void VulkanCanvas::render() {
    frame_rendered_ = false;
    if (surface_ == VK_NULL_HANDLE || window_ == nullptr || backend_ == nullptr ||
        !backend_->context().is_device_ready()) {
        return;
    }

    const glm::ivec2 drawable = window_->drawable_size();
    if (drawable.x <= 0 || drawable.y <= 0) {
        return;
    }

    if (swapchain_ == VK_NULL_HANDLE || swapchain_dirty_ ||
        static_cast<std::uint32_t>(drawable.x) != swapchain_extent_.width ||
        static_cast<std::uint32_t>(drawable.y) != swapchain_extent_.height) {
        if (!recreate_swapchain() || swapchain_ == VK_NULL_HANDLE) {
            return;
        }
    }

    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice device = ctx.device();

    fn.vkWaitForFences(device, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    ctx.note_completed(frame_serials_[current_frame_]);

    const VkResult acquire_res = fn.vkAcquireNextImageKHR(
        device,
        swapchain_,
        UINT64_MAX,
        image_available_semaphores_[current_frame_],
        VK_NULL_HANDLE,
        &acquired_image_index_);

    if (acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return;
    }
    if (acquire_res != VK_SUCCESS && acquire_res != VK_SUBOPTIMAL_KHR) {
        return;
    }
    if (acquire_res == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty_ = true;
    }

    fn.vkResetFences(device, 1, &in_flight_fences_[current_frame_]);
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    fn.vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (fn.vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        return;
    }

    const float clear_alpha = window_->is_transparent() ? 0.0f : 1.0f;
    std::array<VkClearValue, 2> clear_values{};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, clear_alpha}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass_;
    rp_begin.framebuffer = framebuffers_[acquired_image_index_];
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = swapchain_extent_;
    rp_begin.clearValueCount = static_cast<std::uint32_t>(clear_values.size());
    rp_begin.pClearValues = clear_values.data();

    fn.vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    if (ui_renderer_ != nullptr) {
        ui_renderer_->begin_frame(
            static_cast<float>(swapchain_extent_.width), static_cast<float>(swapchain_extent_.height));
    }
    backend_->set_ui_painter(ui_painter());
    backend_->begin_scene_pass(cmd, render_pass_, swapchain_extent_, current_frame_);
    if (commands_ != nullptr) {
        backend_->execute(*commands_);
    }
    backend_->end_scene_pass();
    if (ui_renderer_ != nullptr) {
        if (ui_device_ != nullptr) {
            ui_device_->set_frame_context(cmd, render_pass_, current_frame_);
        }
        ui_renderer_->end_frame();
    }

    fn.vkCmdEndRenderPass(cmd);
    if (fn.vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        return;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_semaphores_[current_frame_];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_semaphores_[current_frame_];

    if (fn.vkQueueSubmit(ctx.graphics_queue(), 1, &submit, in_flight_fences_[current_frame_]) == VK_SUCCESS) {
        frame_serials_[current_frame_] = ctx.note_submitted();
        frame_rendered_ = true;
    }
}

TextureDesc VulkanCanvas::read_pixels() {
    if (window_ == nullptr || window_->window() == nullptr || !frame_rendered_ ||
        swapchain_ == VK_NULL_HANDLE || !swapchain_supports_transfer_src_ || backend_ == nullptr) {
        return {};
    }
    constexpr SDL_WindowFlags kUnseen = SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED;
    if ((SDL_GetWindowFlags(window_->window()) & kUnseen) != 0) {
        return {};
    }
    const int width = static_cast<int>(swapchain_extent_.width);
    const int height = static_cast<int>(swapchain_extent_.height);
    if (width <= 0 || height <= 0) {
        return {};
    }

    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice device = ctx.device();

    fn.vkWaitForFences(device, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    const VkDeviceSize buffer_size =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!ctx.create_buffer(
            buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buf,
            staging_mem)) {
        return {};
    }

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = command_pool_;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer copy_cmd = VK_NULL_HANDLE;
    fn.vkAllocateCommandBuffers(device, &cmd_alloc, &copy_cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    fn.vkBeginCommandBuffer(copy_cmd, &begin_info);

    VkImageMemoryBarrier to_src{};
    to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_src.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.image = swapchain_images_[acquired_image_index_];
    to_src.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    fn.vkCmdPipelineBarrier(
        copy_cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_src);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {swapchain_extent_.width, swapchain_extent_.height, 1};
    fn.vkCmdCopyImageToBuffer(
        copy_cmd,
        swapchain_images_[acquired_image_index_],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging_buf,
        1,
        &region);

    VkImageMemoryBarrier to_present = to_src;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_present.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    fn.vkCmdPipelineBarrier(
        copy_cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_present);

    fn.vkEndCommandBuffer(copy_cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &copy_cmd;
    fn.vkQueueSubmit(ctx.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    fn.vkQueueWaitIdle(ctx.graphics_queue());
    fn.vkFreeCommandBuffers(device, command_pool_, 1, &copy_cmd);

    void* mapped = nullptr;
    std::vector<std::uint8_t> bottom_up(static_cast<std::size_t>(buffer_size));
    if (fn.vkMapMemory(device, staging_mem, 0, buffer_size, 0, &mapped) == VK_SUCCESS && mapped != nullptr) {
        const auto* src = static_cast<const std::uint8_t*>(mapped);
        const std::size_t row = static_cast<std::size_t>(width) * 4u;
        const bool is_bgra = swapchain_format_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             swapchain_format_ == VK_FORMAT_B8G8R8A8_SRGB;
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* src_row = src + static_cast<std::size_t>(y) * row;
            std::uint8_t* dst_row = bottom_up.data() + static_cast<std::size_t>(height - 1 - y) * row;
            for (std::size_t i = 0; i < row; i += 4) {
                dst_row[i + 0] = is_bgra ? src_row[i + 2] : src_row[i + 0];
                dst_row[i + 1] = src_row[i + 1];
                dst_row[i + 2] = is_bgra ? src_row[i + 0] : src_row[i + 2];
                dst_row[i + 3] = src_row[i + 3];
            }
        }
        fn.vkUnmapMemory(device, staging_mem);
    }

    fn.vkDestroyBuffer(device, staging_buf, nullptr);
    fn.vkFreeMemory(device, staging_mem, nullptr);

    return framebuffer_image(bottom_up, width, height, window_->is_transparent());
}

void VulkanCanvas::present() {
    if (!frame_rendered_ || swapchain_ == VK_NULL_HANDLE || backend_ == nullptr) {
        return;
    }
    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[current_frame_];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &acquired_image_index_;

    const VkResult res = fn.vkQueuePresentKHR(ctx.graphics_queue(), &present_info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty_ = true;
    }
    frame_rendered_ = false;
    current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
}

bool VulkanCanvas::create_sync_and_commands() {
    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice device = ctx.device();

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = ctx.graphics_family();
    const VkResult pool_created = fn.vkCreateCommandPool(device, &pool_info, nullptr, &command_pool_);
    if (pool_created != VK_SUCCESS) {
        log_vk_failure("vkCreateCommandPool", pool_created);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = kMaxFramesInFlight;
    const VkResult buffers_allocated =
            fn.vkAllocateCommandBuffers(device, &alloc_info, command_buffers_.data());
    if (buffers_allocated != VK_SUCCESS) {
        log_vk_failure("vkAllocateCommandBuffers", buffers_allocated);
        return false;
    }

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        const VkResult image_sem =
                fn.vkCreateSemaphore(device, &sem_info, nullptr, &image_available_semaphores_[i]);
        const VkResult render_sem =
                fn.vkCreateSemaphore(device, &sem_info, nullptr, &render_finished_semaphores_[i]);
        const VkResult fence = fn.vkCreateFence(device, &fence_info, nullptr, &in_flight_fences_[i]);
        if (image_sem != VK_SUCCESS) {
            log_vk_failure("vkCreateSemaphore", image_sem);
        }
        if (render_sem != VK_SUCCESS) {
            log_vk_failure("vkCreateSemaphore", render_sem);
        }
        if (fence != VK_SUCCESS) {
            log_vk_failure("vkCreateFence", fence);
        }
        if (image_sem != VK_SUCCESS || render_sem != VK_SUCCESS || fence != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

void VulkanCanvas::destroy_sync_and_commands() {
    if (backend_ == nullptr || !backend_->context().is_device_ready()) {
        return;
    }
    const VulkanFunctions& fn = backend_->context().fn();
    const VkDevice device = backend_->context().device();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (image_available_semaphores_[i] != VK_NULL_HANDLE) {
            fn.vkDestroySemaphore(device, image_available_semaphores_[i], nullptr);
            image_available_semaphores_[i] = VK_NULL_HANDLE;
        }
        if (render_finished_semaphores_[i] != VK_NULL_HANDLE) {
            fn.vkDestroySemaphore(device, render_finished_semaphores_[i], nullptr);
            render_finished_semaphores_[i] = VK_NULL_HANDLE;
        }
        if (in_flight_fences_[i] != VK_NULL_HANDLE) {
            fn.vkDestroyFence(device, in_flight_fences_[i], nullptr);
            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        fn.vkDestroyCommandPool(device, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    command_buffers_.fill(VK_NULL_HANDLE);
}

bool VulkanCanvas::recreate_swapchain() {
    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice device = ctx.device();

    fn.vkDeviceWaitIdle(device);
    ctx.release_retired();
    destroy_swapchain();

    const glm::ivec2 drawable = window_->drawable_size();
    if (drawable.x <= 0 || drawable.y <= 0) {
        return true;
    }

    VkSurfaceCapabilitiesKHR caps{};
    const VkResult caps_result =
            fn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device(), surface_, &caps);
    if (caps_result != VK_SUCCESS) {
        log_vk_failure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", caps_result);
        return false;
    }
    if (caps.maxImageExtent.width == 0 || caps.maxImageExtent.height == 0) {
        return true;
    }

    std::uint32_t fmt_count = 0;
    fn.vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device(), surface_, &fmt_count, nullptr);
    if (fmt_count == 0) {
        log::error("Vulkan: the surface has no formats");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    fn.vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device(), surface_, &fmt_count, formats.data());

    std::uint32_t mode_count = 0;
    fn.vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device(), surface_, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count > 0 ? mode_count : 1, VK_PRESENT_MODE_FIFO_KHR);
    if (mode_count > 0) {
        fn.vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device(), surface_, &mode_count, modes.data());
    }

    const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    const VkPresentModeKHR present_mode = choose_present_mode(modes, vsync_);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(
            static_cast<std::uint32_t>(drawable.x), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(
            static_cast<std::uint32_t>(drawable.y), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    swapchain_supports_transfer_src_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (swapchain_supports_transfer_src_) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkSwapchainCreateInfoKHR sc_info{};
    sc_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc_info.surface = surface_;
    sc_info.minImageCount = image_count;
    sc_info.imageFormat = surface_format.format;
    sc_info.imageColorSpace = surface_format.colorSpace;
    sc_info.imageExtent = extent;
    sc_info.imageArrayLayers = 1;
    sc_info.imageUsage = usage;
    sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc_info.preTransform = caps.currentTransform;
    sc_info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha, window_->is_transparent());
    sc_info.presentMode = present_mode;
    sc_info.clipped = VK_TRUE;

    const VkResult swapchain_created = fn.vkCreateSwapchainKHR(device, &sc_info, nullptr, &swapchain_);
    if (swapchain_created != VK_SUCCESS) {
        log_vk_failure("vkCreateSwapchainKHR", swapchain_created);
        swapchain_ = VK_NULL_HANDLE;
        return false;
    }

    swapchain_format_ = surface_format.format;
    swapchain_extent_ = extent;
    depth_format_ = ctx.find_depth_format();

    std::uint32_t actual_count = 0;
    fn.vkGetSwapchainImagesKHR(device, swapchain_, &actual_count, nullptr);
    swapchain_images_.resize(actual_count);
    fn.vkGetSwapchainImagesKHR(device, swapchain_, &actual_count, swapchain_images_.data());

    swapchain_views_.resize(actual_count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < actual_count; ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkResult view_created = fn.vkCreateImageView(device, &view_info, nullptr, &swapchain_views_[i]);
        if (view_created != VK_SUCCESS) {
            log_vk_failure("vkCreateImageView", view_created);
            destroy_swapchain();
            return false;
        }
    }

    if (!create_depth_resources(extent) || !create_render_pass() || !create_framebuffers()) {
        destroy_swapchain();
        return false;
    }

    swapchain_dirty_ = false;
    return true;
}

void VulkanCanvas::destroy_swapchain() {
    if (backend_ == nullptr || !backend_->context().is_device_ready()) {
        return;
    }
    const VulkanFunctions& fn = backend_->context().fn();
    const VkDevice device = backend_->context().device();

    for (VkFramebuffer fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            fn.vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    framebuffers_.clear();

    if (render_pass_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    destroy_depth_resources();

    for (VkImageView view : swapchain_views_) {
        if (view != VK_NULL_HANDLE) {
            fn.vkDestroyImageView(device, view, nullptr);
        }
    }
    swapchain_views_.clear();
    swapchain_images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        fn.vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchain_extent_ = {0, 0};
}

bool VulkanCanvas::create_depth_resources(VkExtent2D extent) {
    VulkanContext& ctx = backend_->context();
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice device = ctx.device();

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = depth_format_;
    img_info.extent = {extent.width, extent.height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkResult image_created = fn.vkCreateImage(device, &img_info, nullptr, &depth_image_);
    if (image_created != VK_SUCCESS) {
        log_vk_failure("vkCreateImage", image_created);
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    fn.vkGetImageMemoryRequirements(device, depth_image_, &mem_reqs);
    const auto mem_type = ctx.find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!mem_type.has_value()) {
        log::error("Vulkan: no memory type for the depth image");
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = *mem_type;
    const VkResult depth_memory = fn.vkAllocateMemory(device, &alloc_info, nullptr, &depth_memory_);
    if (depth_memory != VK_SUCCESS) {
        log_vk_failure("vkAllocateMemory", depth_memory);
        return false;
    }
    fn.vkBindImageMemory(device, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = depth_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth_format_;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    const VkResult depth_view = fn.vkCreateImageView(device, &view_info, nullptr, &depth_view_);
    if (depth_view != VK_SUCCESS) {
        log_vk_failure("vkCreateImageView", depth_view);
    }
    return depth_view == VK_SUCCESS;
}

void VulkanCanvas::destroy_depth_resources() {
    const VulkanFunctions& fn = backend_->context().fn();
    const VkDevice device = backend_->context().device();
    if (depth_view_ != VK_NULL_HANDLE) {
        fn.vkDestroyImageView(device, depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        fn.vkDestroyImage(device, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        fn.vkFreeMemory(device, depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

bool VulkanCanvas::create_render_pass() {
    const VulkanFunctions& fn = backend_->context().fn();
    const VkDevice device = backend_->context().device();

    VkAttachmentDescription color_att{};
    color_att.format = swapchain_format_;
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_att{};
    depth_att.format = depth_format_;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments{color_att, depth_att};

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    rp_info.pAttachments = attachments.data();
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dep;

    const VkResult pass_created = fn.vkCreateRenderPass(device, &rp_info, nullptr, &render_pass_);
    if (pass_created != VK_SUCCESS) {
        log_vk_failure("vkCreateRenderPass", pass_created);
    }
    return pass_created == VK_SUCCESS;
}

bool VulkanCanvas::create_framebuffers() {
    const VulkanFunctions& fn = backend_->context().fn();
    const VkDevice device = backend_->context().device();

    framebuffers_.resize(swapchain_views_.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < swapchain_views_.size(); ++i) {
        std::array<VkImageView, 2> views{swapchain_views_[i], depth_view_};
        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = static_cast<std::uint32_t>(views.size());
        fb_info.pAttachments = views.data();
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;
        const VkResult fb_created = fn.vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers_[i]);
        if (fb_created != VK_SUCCESS) {
            log_vk_failure("vkCreateFramebuffer", fb_created);
            return false;
        }
    }
    return true;
}

} // namespace engine::render
