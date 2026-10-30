#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"
#include "vulkan_shader.h"

#include "ui/render/ui_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine::render {

class VulkanUiDevice final : public ui::IUiDevice {
public:
    static constexpr std::uint32_t kMaxFramesInFlight = 2;

    explicit VulkanUiDevice(std::shared_ptr<VulkanContext> context);
    ~VulkanUiDevice() override;

    VulkanUiDevice(const VulkanUiDevice&) = delete;
    VulkanUiDevice& operator=(const VulkanUiDevice&) = delete;

    [[nodiscard]] bool create();
    void shutdown();

    void set_frame_context(
        VkCommandBuffer cmd,
        VkRenderPass render_pass,
        std::uint32_t frame_index) noexcept;

    void begin_frame(float width, float height, float pixel_ratio) override;
    void create_texture(
        ui::UiTextureSlot slot,
        std::uint32_t page,
        int width,
        int height,
        ui::UiTextureFormat format,
        const ui::UiTextureRegion& texels) override;
    void update_texture(
        ui::UiTextureSlot slot,
        std::uint32_t page,
        const ui::UiTextureRegion& texels) override;
    void release_textures() override;
    void upload(std::span<const ui::UiVertex> vertices, std::span<const ui::UiIndex> indices) override;
    void draw(std::span<const ui::UiBatch> batches) override;
    void end_frame() override;
    [[nodiscard]] int draw_calls() const override { return draw_calls_; }

    [[nodiscard]] const VulkanShader* shader() const noexcept { return shader_.get(); }
    [[nodiscard]] std::size_t texture_page_count(ui::UiTextureSlot slot) const noexcept;

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
        ui::UiTextureFormat format = ui::UiTextureFormat::R8;
        bool valid = false;
    };

    struct FrameBuffers {
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkBuffer ubo_buffer = VK_NULL_HANDLE;
        VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        std::size_t vertex_capacity = 0;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        std::size_t index_capacity = 0;
    };

    [[nodiscard]] bool ensure_gpu_objects();
    [[nodiscard]] bool ensure_pipeline(VkRenderPass render_pass);
    [[nodiscard]] bool ensure_frame_buffers(std::uint32_t frame_index);
    [[nodiscard]] bool allocate_gpu_texture(
        Texture& tex,
        int width,
        int height,
        ui::UiTextureFormat format);
    void destroy_gpu_texture(Texture& tex);
    void upload_texture_region(Texture& tex, const ui::UiTextureRegion& texels);

    std::shared_ptr<VulkanContext> context_;
    std::unique_ptr<VulkanShader> shader_;

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkRenderPass pipeline_render_pass_ = VK_NULL_HANDLE;
    Texture fallback_texture_{};

    std::array<std::vector<Texture>, ui::kUiTextureSlotCount> textures_{};
    std::array<std::uint32_t, ui::kUiTextureSlotCount> bound_pages_{};
    std::array<FrameBuffers, kMaxFramesInFlight> frame_buffers_{};

    VkCommandBuffer active_cmd_ = VK_NULL_HANDLE;
    VkRenderPass active_render_pass_ = VK_NULL_HANDLE;
    std::uint32_t active_frame_ = 0;
    float view_width_ = 0.0f;
    float view_height_ = 0.0f;
    float pixel_ratio_ = 1.0f;
    bool has_uploaded_geometry_ = false;
    int draw_calls_ = 0;
};

} // namespace engine::render
