#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine::render {

class VulkanTexture final : public IDynamicTexture {
public:
    VulkanTexture(std::shared_ptr<VulkanContext> context, const TextureDesc& desc);
    VulkanTexture(std::shared_ptr<VulkanContext> context, const DynamicTextureDesc& desc);
    ~VulkanTexture() override;

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int height() const noexcept override { return height_; }

    bool update(std::span<const std::uint8_t> rgba, int width, int height) override;

    [[nodiscard]] bool ensure_gpu_image();
    [[nodiscard]] VkImageView image_view() const noexcept { return image_view_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }

private:
    void destroy_gpu_image();
    [[nodiscard]] bool upload_pixels();

    std::shared_ptr<VulkanContext> context_;
    int width_ = 0;
    int height_ = 0;
    int gpu_width_ = 0;
    int gpu_height_ = 0;
    FilterMode filter_ = FilterMode::Linear;
    WrapMode wrap_ = WrapMode::Clamp;
    std::vector<std::uint8_t> flipped_rgba_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool dirty_ = true;
    bool valid_ = false;
};

} // namespace engine::render
