#include "vulkan_texture.h"

#include "render/texture_check.h"

#include <engine/log.h>

#include <algorithm>
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
    , wrap_(desc.wrap)
    , mipmaps_(desc.mipmaps) {
    if (!flip_rows_into(desc.rgba, desc.width, desc.height, flipped_rgba_)) {
        return;
    }
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_image();
    }
}

VulkanTexture::VulkanTexture(std::shared_ptr<VulkanContext> context, const TextureArrayDesc& desc)
    : context_(std::move(context))
    , width_(desc.width)
    , height_(desc.height)
    , filter_(desc.filter)
    , wrap_(desc.wrap)
    , mipmaps_(desc.mipmaps) {
    if (const std::string error = texture_array_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }
    if (!flip_layers_vertically_into(desc.rgba, desc.width, desc.height, desc.layers, flipped_rgba_)) {
        return;
    }
    layers_ = desc.layers;
    valid_ = true;
    if (context_ && context_->is_device_ready()) {
        (void)ensure_gpu_image();
    }
}

std::uint32_t VulkanTexture::mip_levels() const noexcept {
    return mipmaps_ == MipmapMode::None ? 1u : static_cast<std::uint32_t>(mip_level_count(width_, height_));
}

std::uint32_t VulkanTexture::image_layers() const noexcept {
    return layers_ > 0 ? static_cast<std::uint32_t>(layers_) : 1u;
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
    std::vector<std::uint8_t> next;
    if (!valid_ || layers_ > 0 || !flip_rows_into(rgba, width, height, next)) {
        return false;
    }
    // False must leave everything as it was, so the old pixels and size wait until the GPU has taken the new ones.
    const int old_width = width_;
    const int old_height = height_;
    const bool old_dirty = dirty_;
    flipped_rgba_.swap(next);
    width_ = width;
    height_ = height;
    dirty_ = true;
    if (context_ && context_->is_device_ready() && !ensure_gpu_image()) {
        flipped_rgba_.swap(next);
        width_ = old_width;
        height_ = old_height;
        dirty_ = old_dirty;
        return false;
    }
    return true;
}

bool VulkanTexture::update_rects(
        std::span<const TextureRect> rects, std::span<const std::uint8_t> rgba, int row_stride) {
    if (!valid_ || layers_ > 0 || mipmaps_ != MipmapMode::None) {
        return false;
    }
    if (rects.empty()) {
        return true;
    }
    if (const std::string error = texture_rects_error(width_, height_, rects, rgba, row_stride); !error.empty()) {
        log::error(error);
        return false;
    }
    // A partial copy is safe only over an image that already holds this CPU copy. Anything pending, or no image
    // yet, stays in the CPU buffer and goes up with the next full upload.
    const bool can_patch = context_ && context_->is_device_ready() && image_ != VK_NULL_HANDLE &&
            gpu_width_ == width_ && gpu_height_ == height_ && !dirty_;
    // False must leave everything as it was, so the CPU copy changes only once the GPU has taken the rectangles.
    if (can_patch) {
        return upload_rects(rects, rgba, row_stride) &&
                write_texture_rects(width_, height_, rects, rgba, row_stride, flipped_rgba_);
    }
    std::vector<std::uint8_t> before = flipped_rgba_;
    if (!write_texture_rects(width_, height_, rects, rgba, row_stride, flipped_rgba_)) {
        return false;
    }
    const bool old_dirty = dirty_;
    dirty_ = true;
    if (context_ && context_->is_device_ready() && !ensure_gpu_image()) {
        flipped_rgba_ = std::move(before);
        dirty_ = old_dirty;
        return false;
    }
    return true;
}

bool VulkanTexture::upload_rects(
        std::span<const TextureRect> rects, std::span<const std::uint8_t> rgba, int row_stride) {
    if (image_ == VK_NULL_HANDLE || rects.empty()) {
        return false;
    }
    std::size_t bytes = 0;
    for (const TextureRect& rect : rects) {
        bytes += static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u;
    }
    std::vector<std::uint8_t> staging(bytes);
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(rects.size());
    std::size_t offset = 0;
    for (const TextureRect& rect : rects) {
        const std::size_t rect_bytes =
                static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u;
        if (!pack_texture_rect(rect, rgba, row_stride, std::span<std::uint8_t>(staging.data() + offset, rect_bytes))) {
            return false;
        }
        VkBufferImageCopy region{};
        region.bufferOffset = static_cast<VkDeviceSize>(offset);
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {rect.x, texture_rect_gpu_y(height_, rect), 0};
        region.imageExtent = {static_cast<std::uint32_t>(rect.width), static_cast<std::uint32_t>(rect.height), 1u};
        regions.push_back(region);
        offset += rect_bytes;
    }

    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!context_->upload_host_buffer(static_cast<VkDeviceSize>(staging.size()), staging.data(),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buf, staging_mem)) {
        return false;
    }
    VkCommandBuffer cmd = context_->begin_single_time_commands();
    if (cmd == VK_NULL_HANDLE) {
        fn.vkDestroyBuffer(device, staging_buf, nullptr);
        fn.vkFreeMemory(device, staging_mem, nullptr);
        return false;
    }

    // The rest of the image must survive, so this is not the UNDEFINED transition a full upload uses.
    VkImageMemoryBarrier to_dst{};
    to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image = image_;
    to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
            0, nullptr, 1, &to_dst);

    fn.vkCmdCopyBufferToImage(cmd, staging_buf, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<std::uint32_t>(regions.size()), regions.data());

    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
            0, nullptr, 1, &to_read);

    context_->end_single_time_commands(cmd);
    fn.vkDestroyBuffer(device, staging_buf, nullptr);
    fn.vkFreeMemory(device, staging_mem, nullptr);
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

    // The old image stays the texture until the new one holds the pixels.
    const GpuImage old = take_gpu_image();
    if (!create_gpu_image()) {
        destroy_gpu_image();
        put_gpu_image(old);
        return false;
    }
    const GpuImage created = take_gpu_image();
    put_gpu_image(old);
    destroy_gpu_image();
    put_gpu_image(created);
    return true;
}

VulkanTexture::GpuImage VulkanTexture::take_gpu_image() noexcept {
    const GpuImage taken{image_, memory_, image_view_, sampler_, gpu_width_, gpu_height_};
    put_gpu_image(GpuImage{});
    return taken;
}

void VulkanTexture::put_gpu_image(const GpuImage& image) noexcept {
    image_ = image.image;
    memory_ = image.memory;
    image_view_ = image.view;
    sampler_ = image.sampler;
    gpu_width_ = image.width;
    gpu_height_ = image.height;
}

bool VulkanTexture::create_gpu_image() {
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
    img_info.mipLevels = mip_levels();
    img_info.arrayLayers = image_layers();
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    // Each mip level is blitted from the one above, so the image is a transfer source too.
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    view_info.viewType = layers_ > 0 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels(), 0, image_layers()};
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
    samp_info.mipmapMode =
        mipmaps_ == MipmapMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samp_info.minLod = 0.0f;
    // Without mipmaps maxLod 0 keeps sampling level 0, as before mipmaps existed.
    samp_info.maxLod = static_cast<float>(mip_levels() - 1u);

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
    const std::uint32_t levels = mip_levels();
    const std::uint32_t layer_count = image_layers();
    to_dst.image = image_;
    to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layer_count};

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

    // Every layer at once: the staging buffer holds them back to back, tightly packed.
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count};
    region.imageExtent = {
        static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_),
        1u};
    fn.vkCmdCopyBufferToImage(
        cmd, staging_buf, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // The mip chain: level i is level i - 1 halved by a linear blit (R8G8B8A8_UNORM with optimal tiling always
    // supports blits and linear filtering). Each source level goes to TRANSFER_SRC first, then to shader read.
    std::int32_t mip_w = width_;
    std::int32_t mip_h = height_;
    for (std::uint32_t level = 1; level < levels; ++level) {
        VkImageMemoryBarrier to_src = to_dst;
        to_src.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 1, 0, layer_count};
        fn.vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &to_src);

        const std::int32_t next_w = std::max(mip_w / 2, 1);
        const std::int32_t next_h = std::max(mip_h / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 0, layer_count};
        blit.srcOffsets[1] = {mip_w, mip_h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layer_count};
        blit.dstOffsets[1] = {next_w, next_h, 1};
        fn.vkCmdBlitImage(
            cmd,
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR);

        VkImageMemoryBarrier src_to_read = to_src;
        src_to_read.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        src_to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        fn.vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &src_to_read);

        mip_w = next_w;
        mip_h = next_h;
    }

    // The last level (level 0 without mipmaps) was only written.
    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_read.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, levels - 1u, 1, 0, layer_count};

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
