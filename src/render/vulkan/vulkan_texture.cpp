#include "vulkan_texture.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace engine::render {

namespace {

bool flip_rows_into(
    std::span<const std::uint8_t> rgba,
    int width,
    int height,
    std::vector<std::uint8_t>& out) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < required) {
        return false;
    }
    out.resize(required);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height; ++y) {
        const std::size_t src_offset = static_cast<std::size_t>(y) * row_bytes;
        const std::size_t dst_offset = static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::memcpy(out.data() + dst_offset, rgba.data() + src_offset, row_bytes);
    }
    return true;
}

VkFilter vk_filter(FilterMode filter) {
    return filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode vk_address_mode(WrapMode wrap) {
    switch (wrap) {
    case WrapMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case WrapMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case WrapMode::Clamp:
    default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

} // namespace

VulkanTexture::VulkanTexture(std::shared_ptr<VulkanContext> context, const TextureDesc& desc)
    : context_(std::move(context))
    , width_(desc.width)
    , height_(desc.height)
    , filter_(desc.filter)
    , wrap_(desc.wrap) {
    if (!flip_rows_into(desc.rgba, desc.width, desc.height, flipped_rgba_)) {
        return;
    }
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_image();
    }
}

VulkanTexture::VulkanTexture(std::shared_ptr<VulkanContext> context, const DynamicTextureDesc& desc)
    : context_(std::move(context))
    , width_(desc.width)
    , height_(desc.height)
    , filter_(desc.filter)
    , wrap_(desc.wrap) {
    if (desc.width <= 0 || desc.height <= 0) {
        return;
    }
    const std::size_t bytes =
        static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4u;
    flipped_rgba_.assign(bytes, 0);
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_image();
    }
}

VulkanTexture::~VulkanTexture() {
    destroy_gpu_image();
}

bool VulkanTexture::update(std::span<const std::uint8_t> rgba, int width, int height) {
    if (!valid_ || !flip_rows_into(rgba, width, height, flipped_rgba_)) {
        return false;
    }
    width_ = width;
    height_ = height;
    dirty_ = true;
    if (context_ && context_->is_device_ready()) {
        return ensure_gpu_image();
    }
    return true;
}

bool VulkanTexture::ensure_gpu_image() {
    if (!valid_ || !context_ || !context_->is_device_ready()) {
        return false;
    }
    if (image_ != VK_NULL_HANDLE && gpu_width_ == width_ && gpu_height_ == height_) {
        if (!dirty_) {
            return true;
        }
        return upload_pixels();
    }

    destroy_gpu_image();

    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = {
        static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_),
        1u};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (fn.vkCreateImage(device, &img_info, nullptr, &image_) != VK_SUCCESS) {
        image_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements reqs{};
    fn.vkGetImageMemoryRequirements(device, image_, &reqs);
    const auto mem_type =
        context_->find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!mem_type.has_value()) {
        destroy_gpu_image();
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = *mem_type;
    if (fn.vkAllocateMemory(device, &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        destroy_gpu_image();
        return false;
    }
    if (fn.vkBindImageMemory(device, image_, memory_, 0) != VK_SUCCESS) {
        destroy_gpu_image();
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (fn.vkCreateImageView(device, &view_info, nullptr, &image_view_) != VK_SUCCESS) {
        destroy_gpu_image();
        return false;
    }

    const VkFilter filter = vk_filter(filter_);
    const VkSamplerAddressMode addr = vk_address_mode(wrap_);

    VkSamplerCreateInfo samp_info{};
    samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_info.magFilter = filter;
    samp_info.minFilter = filter;
    samp_info.addressModeU = addr;
    samp_info.addressModeV = addr;
    samp_info.addressModeW = addr;
    samp_info.anisotropyEnable = VK_FALSE;
    samp_info.maxAnisotropy = 1.0f;
    samp_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samp_info.unnormalizedCoordinates = VK_FALSE;
    samp_info.compareEnable = VK_FALSE;
    samp_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (fn.vkCreateSampler(device, &samp_info, nullptr, &sampler_) != VK_SUCCESS) {
        destroy_gpu_image();
        return false;
    }

    gpu_width_ = width_;
    gpu_height_ = height_;
    return upload_pixels();
}

bool VulkanTexture::upload_pixels() {
    if (image_ == VK_NULL_HANDLE || flipped_rgba_.empty()) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(flipped_rgba_.size());
    if (!context_->upload_host_buffer(
            byte_size,
            flipped_rgba_.data(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            staging_buf,
            staging_mem)) {
        return false;
    }

    VkCommandBuffer cmd = context_->begin_single_time_commands();
    if (cmd == VK_NULL_HANDLE) {
        fn.vkDestroyBuffer(device, staging_buf, nullptr);
        fn.vkFreeMemory(device, staging_mem, nullptr);
        return false;
    }

    VkImageMemoryBarrier to_dst{};
    to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.srcAccessMask = 0;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image = image_;
    to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    fn.vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_dst);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {
        static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_),
        1u};
    fn.vkCmdCopyBufferToImage(
        cmd, staging_buf, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    fn.vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_read);

    context_->end_single_time_commands(cmd);

    fn.vkDestroyBuffer(device, staging_buf, nullptr);
    fn.vkFreeMemory(device, staging_mem, nullptr);
    dirty_ = false;
    return true;
}

void VulkanTexture::destroy_gpu_image() {
    if (!context_ || !context_->is_device_ready()) {
        sampler_ = VK_NULL_HANDLE;
        image_view_ = VK_NULL_HANDLE;
        image_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (sampler_ != VK_NULL_HANDLE && fn.vkDestroySampler) {
        fn.vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (image_view_ != VK_NULL_HANDLE && fn.vkDestroyImageView) {
        fn.vkDestroyImageView(device, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE && fn.vkDestroyImage) {
        fn.vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE && fn.vkFreeMemory) {
        fn.vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    gpu_width_ = 0;
    gpu_height_ = 0;
}

} // namespace engine::render
