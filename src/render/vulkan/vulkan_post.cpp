#include "vulkan_post.h"

#include <engine/log.h>

#include <algorithm>

namespace engine::render {

namespace {

[[nodiscard]] VkFormat vk_format(SceneColorFormat format) {
    if (format == SceneColorFormat::R11G11B10) {
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    }
    if (format == SceneColorFormat::Rgba16F) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

VkRenderPass make_color_pass(const VulkanFunctions& fn, VkDevice device, VkFormat color, bool clear, bool with_depth,
        VkFormat depth, bool sample_depth) {
    VkAttachmentDescription color_att{};
    color_att.format = color;
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth_att{};
    depth_att.format = depth;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout = sample_depth ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    std::array<VkAttachmentDescription, 2> attachments{color_att, depth_att};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    if (with_depth) {
        subpass.pDepthStencilAttachment = &depth_ref;
    }

    // The previous pass may have written this image (or a depth image this pass samples). The next pass samples it.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = with_depth ? 2u : 1u;
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(deps.size());
    info.pDependencies = deps.data();
    VkRenderPass pass = VK_NULL_HANDLE;
    if (fn.vkCreateRenderPass(device, &info, nullptr, &pass) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pass;
}

} // namespace

void VulkanPost::set_frame(VulkanContext* context, VkCommandBuffer cmd, VkFramebuffer window_fb, VkExtent2D extent,
        VkFormat window_color, VkFormat window_depth, float clear_alpha) {
    context_ = context;
    cmd_ = cmd;
    window_fb_ = window_fb;
    extent_ = extent;
    window_color_ = window_color;
    window_depth_ = window_depth;
    clear_alpha_ = clear_alpha;
    pass_open_ = false;
}

void VulkanPost::shutdown() {
    destroy_targets();
    if (context_ == nullptr || !context_->is_device_ready()) {
        scene_pass_ = VK_NULL_HANDLE;
        color_pass_ = VK_NULL_HANDLE;
        window_load_ = VK_NULL_HANDLE;
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (scene_pass_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, scene_pass_, nullptr);
    }
    if (color_pass_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, color_pass_, nullptr);
    }
    if (window_load_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, window_load_, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE) {
        fn.vkDestroySampler(device, sampler_, nullptr);
    }
    if (depth_sampler_ != VK_NULL_HANDLE) {
        fn.vkDestroySampler(device, depth_sampler_, nullptr);
    }
    scene_pass_ = color_pass_ = window_load_ = VK_NULL_HANDLE;
    sampler_ = depth_sampler_ = VK_NULL_HANDLE;
    context_ = nullptr;
}

bool VulkanPost::format_supported(VkFormat format) const {
    if (context_ == nullptr) {
        return false;
    }
    VkFormatProperties props{};
    context_->fn().vkGetPhysicalDeviceFormatProperties(context_->physical_device(), format, &props);
    const VkFormatFeatureFlags need =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & need) == need;
}

bool VulkanPost::create_image(Slot& slot, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage,
        VkImageAspectFlags aspect) {
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = usage;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (fn.vkCreateImage(device, &image, nullptr, &slot.image) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements reqs{};
    fn.vkGetImageMemoryRequirements(device, slot.image, &reqs);
    const auto type = context_->find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) {
        return false;
    }
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = *type;
    if (fn.vkAllocateMemory(device, &alloc, nullptr, &slot.memory) != VK_SUCCESS ||
            fn.vkBindImageMemory(device, slot.image, slot.memory, 0) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = slot.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    return fn.vkCreateImageView(device, &view, nullptr, &slot.view) == VK_SUCCESS;
}

void VulkanPost::destroy_slot(Slot& slot) {
    if (context_ == nullptr || !context_->is_device_ready()) {
        slot = Slot{};
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (slot.fb != VK_NULL_HANDLE) {
        fn.vkDestroyFramebuffer(device, slot.fb, nullptr);
    }
    if (slot.view != VK_NULL_HANDLE) {
        fn.vkDestroyImageView(device, slot.view, nullptr);
    }
    if (slot.image != VK_NULL_HANDLE) {
        fn.vkDestroyImage(device, slot.image, nullptr);
    }
    if (slot.memory != VK_NULL_HANDLE) {
        fn.vkFreeMemory(device, slot.memory, nullptr);
    }
    slot = Slot{};
}

void VulkanPost::destroy_targets() {
    if (context_ != nullptr && context_->is_device_ready() && (color_[0].image != VK_NULL_HANDLE || depth_.image != VK_NULL_HANDLE)) {
        context_->fn().vkQueueWaitIdle(context_->graphics_queue());
    }
    if (context_ != nullptr && context_->is_device_ready() && color0_color_fb_ != VK_NULL_HANDLE) {
        context_->fn().vkDestroyFramebuffer(context_->device(), color0_color_fb_, nullptr);
        color0_color_fb_ = VK_NULL_HANDLE;
    }
    destroy_slot(color_[0]);
    destroy_slot(color_[1]);
    for (Slot& level : bloom_) {
        destroy_slot(level);
    }
    destroy_slot(depth_);
    width_ = 0;
    height_ = 0;
}

bool VulkanPost::ensure_passes() {
    if (scene_pass_ != VK_NULL_HANDLE && color_pass_ != VK_NULL_HANDLE && window_load_ != VK_NULL_HANDLE &&
            pass_window_color_ == window_color_ && pass_window_depth_ == window_depth_ &&
            scene_format_ == vk_format(targets_format_) && pass_samples_depth_ == depth_texture_) {
        return true;
    }
    if (context_ == nullptr || !context_->is_device_ready()) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (scene_pass_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, scene_pass_, nullptr);
    }
    if (color_pass_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, color_pass_, nullptr);
    }
    if (window_load_ != VK_NULL_HANDLE) {
        fn.vkDestroyRenderPass(device, window_load_, nullptr);
    }
    const VkFormat scene = vk_format(targets_format_);
    const bool sample_depth = depth_texture_;
    scene_pass_ = make_color_pass(fn, device, scene, true, true, VK_FORMAT_D32_SFLOAT, sample_depth);
    color_pass_ = make_color_pass(fn, device, scene, false, false, VK_FORMAT_D32_SFLOAT, false);

    VkAttachmentDescription color_att{};
    color_att.format = window_color_;
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription depth_att{};
    depth_att.format = window_depth_;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    std::array<VkAttachmentDescription, 2> attachments{color_att, depth_att};
    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = 0;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(deps.size());
    info.pDependencies = deps.data();
    window_load_ = VK_NULL_HANDLE;
    if (fn.vkCreateRenderPass(device, &info, nullptr, &window_load_) != VK_SUCCESS) {
        window_load_ = VK_NULL_HANDLE;
    }
    pass_window_color_ = window_color_;
    pass_window_depth_ = window_depth_;
    scene_format_ = scene;
    pass_samples_depth_ = depth_texture_;
    return scene_pass_ != VK_NULL_HANDLE && color_pass_ != VK_NULL_HANDLE && window_load_ != VK_NULL_HANDLE;
}

bool VulkanPost::ensure_targets(SceneColorFormat format, bool depth_texture) {
    const int width = static_cast<int>(extent_.width);
    const int height = static_cast<int>(extent_.height);
    if (width_ == width && height_ == height && targets_format_ == format && depth_texture_ == depth_texture &&
            color_[0].image != VK_NULL_HANDLE && scene_pass_ != VK_NULL_HANDLE) {
        return true;
    }
    destroy_targets();
    targets_format_ = format;
    depth_texture_ = depth_texture;
    if (!ensure_passes()) {
        return false;
    }
    targets_format_ = format;
    depth_texture_ = depth_texture;
    width_ = width;
    height_ = height;
    const VkFormat scene = vk_format(format);
    const VkImageUsageFlags color_usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!create_image(color_[0], scene, extent_, color_usage, VK_IMAGE_ASPECT_COLOR_BIT) ||
            !create_image(color_[1], scene, extent_, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    const VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          (depth_texture ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    if (!create_image(depth_, VK_FORMAT_D32_SFLOAT, extent_, depth_usage, VK_IMAGE_ASPECT_DEPTH_BIT)) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    auto make_fb = [&](Slot& slot, VkRenderPass pass, bool with_depth) {
        std::array<VkImageView, 2> views{slot.view, depth_.view};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = pass;
        fb.attachmentCount = with_depth ? 2u : 1u;
        fb.pAttachments = views.data();
        fb.width = extent_.width;
        fb.height = extent_.height;
        fb.layers = 1;
        return fn.vkCreateFramebuffer(device, &fb, nullptr, &slot.fb) == VK_SUCCESS;
    };
    if (!make_fb(color_[0], scene_pass_, true) || !make_fb(color_[1], color_pass_, false)) {
        return false;
    }
    VkFramebufferCreateInfo color0_fb{};
    color0_fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    color0_fb.renderPass = color_pass_;
    color0_fb.attachmentCount = 1;
    color0_fb.pAttachments = &color_[0].view;
    color0_fb.width = extent_.width;
    color0_fb.height = extent_.height;
    color0_fb.layers = 1;
    if (fn.vkCreateFramebuffer(device, &color0_fb, nullptr, &color0_color_fb_) != VK_SUCCESS) {
        color0_color_fb_ = VK_NULL_HANDLE;
        return false;
    }
    int level_w = std::max(1, width / 2);
    int level_h = std::max(1, height / 2);
    for (int level = 0; level < 3; ++level) {
        bloom_width_[level] = level_w;
        bloom_height_[level] = level_h;
        const VkExtent2D level_extent{static_cast<std::uint32_t>(level_w), static_cast<std::uint32_t>(level_h)};
        if (!create_image(bloom_[level], scene, level_extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return false;
        }
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = color_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &bloom_[level].view;
        fb.width = level_extent.width;
        fb.height = level_extent.height;
        fb.layers = 1;
        if (fn.vkCreateFramebuffer(device, &fb, nullptr, &bloom_[level].fb) != VK_SUCCESS) {
            return false;
        }
        level_w = std::max(1, level_w / 2);
        level_h = std::max(1, level_h / 2);
    }
    if (sampler_ == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0.0f;
        if (fn.vkCreateSampler(device, &sampler, nullptr, &sampler_) != VK_SUCCESS) {
            sampler_ = VK_NULL_HANDLE;
            return false;
        }
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        if (fn.vkCreateSampler(device, &sampler, nullptr, &depth_sampler_) != VK_SUCCESS) {
            depth_sampler_ = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

void VulkanPost::begin_pass(VkRenderPass pass, VkFramebuffer fb, VkExtent2D extent, bool clear_color, bool clear_depth) {
    if (cmd_ == VK_NULL_HANDLE || pass == VK_NULL_HANDLE || fb == VK_NULL_HANDLE) {
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    if (pass_open_) {
        fn.vkCmdEndRenderPass(cmd_);
        pass_open_ = false;
    }
    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, clear_alpha_}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = pass;
    begin.framebuffer = fb;
    begin.renderArea.extent = extent;
    begin.clearValueCount = clear_depth ? 2u : 1u;
    begin.pClearValues = clears.data();
    if (!clear_color && !clear_depth) {
        begin.clearValueCount = 0;
        begin.pClearValues = nullptr;
    }
    fn.vkCmdBeginRenderPass(cmd_, &begin, VK_SUBPASS_CONTENTS_INLINE);
    pass_open_ = true;
}

bool VulkanPost::begin_scene(const PostChain& chain) {
    if (context_ == nullptr || cmd_ == VK_NULL_HANDLE || extent_.width == 0 || extent_.height == 0) {
        return false;
    }
    if (!pass_open_) {
        context_->fn().vkCmdEndRenderPass(cmd_);
    }
    if (!formats_checked_) {
        formats_checked_ = true;
        r11_ok_ = format_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        rgba16_ok_ = format_supported(VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    const bool transparent = clear_alpha_ < 1.0f;
    const bool supported = transparent ? rgba16_ok_ : r11_ok_;
    hdr_effective_ = chain.hdr && supported;
    if (chain.hdr && !supported && !fallback_logged_) {
        fallback_logged_ = true;
        log::warn("Post stack HDR has no float color attachment; the stack runs in RGBA8");
    }
    const SceneColorFormat format = scene_color_format(hdr_effective_, transparent, hdr_effective_ || !chain.hdr);
    if (!ensure_targets(format, chain.samples_depth())) {
        log::error("Vulkan: the post scene target was not created");
        if (window_load_ != VK_NULL_HANDLE) {
            begin_window_load();
        }
        return false;
    }
    begin_pass(scene_pass_, color_[0].fb, extent_, true, true);
    scene_bound_ = true;
    return true;
}

void VulkanPost::begin(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clear_color,
        bool clear_depth) {
    begin_pass(pass, framebuffer, extent, clear_color, clear_depth);
}

void VulkanPost::begin_window_load() {
    if (context_ == nullptr || cmd_ == VK_NULL_HANDLE) {
        scene_bound_ = false;
        return;
    }
    begin_pass(window_load_, window_fb_, extent_, false, false);
    scene_bound_ = false;
}

VkImageView VulkanPost::scene_view(PostImage image) const noexcept {
    return image == PostImage::SceneB ? color_[1].view : color_[0].view;
}

VkImageView VulkanPost::bloom_view(int level) const noexcept {
    return level >= 0 && level < 3 ? bloom_[level].view : VK_NULL_HANDLE;
}

VkFramebuffer VulkanPost::scene_fb(PostImage image) const noexcept {
    if (image == PostImage::SceneB) {
        return color_[1].fb;
    }
    return color0_color_fb_ != VK_NULL_HANDLE ? color0_color_fb_ : color_[0].fb;
}

VkFramebuffer VulkanPost::bloom_fb(int level) const noexcept {
    return level >= 0 && level < 3 ? bloom_[level].fb : VK_NULL_HANDLE;
}

} // namespace engine::render
