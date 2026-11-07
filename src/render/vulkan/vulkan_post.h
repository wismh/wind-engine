#pragma once

#include "vulkan_context.h"

#include <engine/render/post_process.h>

#include <cstdint>

namespace engine::render {

// Offscreen color (and optional depth) for one window. Recreated when the drawable size or the HDR format changes.
// Empty until a view with an active stack begins the scene pass.
class VulkanPost {
public:
    void set_frame(VulkanContext* context, VkCommandBuffer cmd, VkFramebuffer window_fb, VkExtent2D extent,
            VkFormat window_color, VkFormat window_depth, float clear_alpha);
    void shutdown();

    // Ends the window pass the canvas opened and begins the scene pass. False when the surface is empty.
    bool begin_scene(const PostChain& chain);
    void begin_window_load();
    void begin(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clear_color, bool clear_depth);
    [[nodiscard]] bool scene_bound() const noexcept { return scene_bound_; }
    void release_scene() noexcept { scene_bound_ = false; }
    [[nodiscard]] bool hdr_effective() const noexcept { return hdr_effective_; }

    [[nodiscard]] VkImageView scene_view(PostImage image) const noexcept;
    [[nodiscard]] VkImageView bloom_view(int level) const noexcept;
    [[nodiscard]] VkFramebuffer scene_fb(PostImage image) const noexcept;
    [[nodiscard]] VkFramebuffer bloom_fb(int level) const noexcept;
    [[nodiscard]] VkRenderPass scene_pass() const noexcept { return scene_pass_; }
    [[nodiscard]] VkRenderPass color_pass() const noexcept { return color_pass_; }
    [[nodiscard]] VkRenderPass window_load_pass() const noexcept { return window_load_; }
    [[nodiscard]] VkFramebuffer window_fb() const noexcept { return window_fb_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkImageView depth_view() const noexcept { return depth_texture_ ? depth_.view : VK_NULL_HANDLE; }
    [[nodiscard]] bool pass_open() const noexcept { return pass_open_; }
    [[nodiscard]] VkSampler depth_sampler() const noexcept { return depth_sampler_; }

    // A copy of the scene depth taken after the opaque draws, plus the render pass that continues the scene
    // while that copy is sampled. `has_depth` is false when the scene has no depth texture.
    struct DepthSample {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkRenderPass pass = VK_NULL_HANDLE;
        bool has_depth = false;
    };
    [[nodiscard]] DepthSample prepare_depth_sample();
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] int bloom_width(int level) const noexcept { return level >= 0 && level < 3 ? bloom_width_[level] : 0; }
    [[nodiscard]] int bloom_height(int level) const noexcept {
        return level >= 0 && level < 3 ? bloom_height_[level] : 0;
    }

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer fb = VK_NULL_HANDLE;
    };

    [[nodiscard]] bool format_supported(VkFormat format) const;
    [[nodiscard]] bool create_image(Slot& slot, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage,
            VkImageAspectFlags aspect);
    void destroy_slot(Slot& slot);
    void destroy_targets();
    [[nodiscard]] bool ensure_passes();
    [[nodiscard]] bool ensure_targets(SceneColorFormat format, bool depth_texture);
    void begin_pass(VkRenderPass pass, VkFramebuffer fb, VkExtent2D extent, bool clear_color, bool clear_depth);

    VulkanContext* context_ = nullptr;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFramebuffer window_fb_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat window_color_ = VK_FORMAT_UNDEFINED;
    VkFormat window_depth_ = VK_FORMAT_UNDEFINED;
    float clear_alpha_ = 1.0f;

    VkRenderPass scene_pass_ = VK_NULL_HANDLE;
    VkRenderPass color_pass_ = VK_NULL_HANDLE;
    VkRenderPass window_load_ = VK_NULL_HANDLE;
    // Same attachments as `scene_pass_`, loading color and depth so decals and later draws continue after the copy.
    VkRenderPass scene_resume_ = VK_NULL_HANDLE;
    VkFormat scene_format_ = VK_FORMAT_UNDEFINED;
    VkFormat pass_window_color_ = VK_FORMAT_UNDEFINED;
    VkFormat pass_window_depth_ = VK_FORMAT_UNDEFINED;

    Slot color_[2]{};
    // color_[0] is also written by later passes, which have no depth. That framebuffer matches `color_pass_`.
    VkFramebuffer color0_color_fb_ = VK_NULL_HANDLE;
    Slot bloom_[3]{};
    Slot depth_{};
    Slot depth_copy_{};
    VkImageLayout depth_copy_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    bool depth_texture_ = false;
    bool depth_copied_ = false;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler depth_sampler_ = VK_NULL_HANDLE;
    int bloom_width_[3]{};
    int bloom_height_[3]{};
    int width_ = 0;
    int height_ = 0;
    SceneColorFormat targets_format_ = SceneColorFormat::Rgba8;

    bool scene_bound_ = false;
    bool pass_open_ = false;
    bool hdr_effective_ = false;
    bool fallback_logged_ = false;
    bool pass_samples_depth_ = false;
    bool r11_ok_ = false;
    bool rgba16_ok_ = false;
    bool formats_checked_ = false;
};

} // namespace engine::render
