#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"
#include "vulkan_post.h"
#include "vulkan_shader.h"
#include "vulkan_texture.h"

#include <engine/render/backend.h>
#include <engine/render/commands.h>
#include <engine/render/material.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine::ui {
class IUiPainter;
struct PaintResources;
} // namespace engine::ui

namespace engine::render {

class VulkanRenderBackend final : public IRenderBackend {
public:
    static constexpr std::uint32_t kMaxFrameSlots = 2;

    explicit VulkanRenderBackend(std::shared_ptr<VulkanContext> context);
    ~VulkanRenderBackend() override;

    VulkanRenderBackend(const VulkanRenderBackend&) = delete;
    VulkanRenderBackend& operator=(const VulkanRenderBackend&) = delete;

    void set_ui_painter(ui::IUiPainter* painter) noexcept { ui_painter_ = painter; }
    [[nodiscard]] ui::PaintResources& ui_resources() noexcept { return *ui_resources_; }
    [[nodiscard]] VulkanContext& context() noexcept { return *context_; }
    [[nodiscard]] const VulkanContext& context() const noexcept { return *context_; }
    [[nodiscard]] const std::shared_ptr<VulkanContext>& context_ptr() const noexcept { return context_; }
    [[nodiscard]] const CmdSetView& last_view() const noexcept { return view_; }

    void begin_scene_pass(
        VkCommandBuffer cmd,
        VkRenderPass render_pass,
        VkExtent2D extent,
        std::uint32_t frame_slot);
    void end_scene_pass() noexcept;

    void set_post_frame(VkCommandBuffer cmd, VkFramebuffer window_fb, VkExtent2D extent, VkFormat window_color,
            VkFormat window_depth, float clear_alpha);
    void set_view(const CmdSetView& view);
    void finish_post();
    void execute_draw_mesh(const CmdDrawMesh& cmd);
    void execute_draw_decal(const CmdDrawDecal& cmd);
    void execute_draw_particles(const CmdDrawParticles& cmd);
    void execute_draw_mesh_particles(const CmdDrawParticles& cmd);
    void execute(const CommandBuffer& commands) override;

    void shutdown_gpu_resources();

private:
    struct FrameSlot {
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkBuffer ubo_buffer = VK_NULL_HANDLE;
        VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
        std::uint8_t* ubo_mapped = nullptr;
        VkDeviceSize ubo_capacity = 0;
        VkDeviceSize ubo_offset = 0;

        VkBuffer instance_buffer = VK_NULL_HANDLE;
        VkDeviceMemory instance_memory = VK_NULL_HANDLE;
        std::uint8_t* instance_mapped = nullptr;
        VkDeviceSize instance_capacity = 0;
        VkDeviceSize instance_offset = 0;
    };

    struct PipelineKey {
        VkShaderModule vert = VK_NULL_HANDLE;
        VkShaderModule frag = VK_NULL_HANDLE;
        RenderState state{};
        VkRenderPass render_pass = VK_NULL_HANDLE;
        bool is_particle = false;
        bool fullscreen = false;

        [[nodiscard]] bool operator==(const PipelineKey&) const noexcept = default;
    };

    struct CachedPipeline {
        PipelineKey key{};
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    [[nodiscard]] bool ensure_shared_objects();
    [[nodiscard]] bool ensure_frame_slot(std::uint32_t slot);
    [[nodiscard]] bool ensure_particle_resources();
    // One MeshParticleInstance (position 0, scale 1, color white, layer 0) for ordinary draws of a shader that
    // declares locations 7-9.
    [[nodiscard]] bool ensure_mesh_instance_dummy();
    // The override's or material's albedo when its kind matches the shader's sampler (`arrayed`: a texture array),
    // otherwise a white 2D texture or a white one-layer array, so the descriptor's view type always matches.
    [[nodiscard]] VulkanTexture* resolve_albedo(
        const IMaterial& material,
        const MaterialOverride* override_params,
        bool arrayed);
    [[nodiscard]] VkPipeline get_or_create_pipeline(
        VulkanShader& shader,
        const RenderState& state,
        bool is_particle,
        bool fullscreen = false);
    // Binding 2. When set, this image is written instead of `normal` (a decal's depth copy).
    struct DepthSamplerBind {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    [[nodiscard]] VkDescriptorSet allocate_and_write_descriptor(
        const std::vector<std::uint8_t>& ubo_bytes,
        VulkanTexture& texture,
        VulkanTexture* normal = nullptr,
        const DepthSamplerBind* depth = nullptr);
    void apply_viewport_and_scissor(bool clear_if_requested);
    void adopt_pass(VkRenderPass pass, VkExtent2D extent);
    void draw_post_shader(const std::shared_ptr<IShader>& shader, const glm::vec4& params, const glm::vec2& resolution,
            float time, VkImageView source, VkImageView depth, VkFramebuffer framebuffer, VkRenderPass pass,
            VkExtent2D extent, bool additive);

    std::shared_ptr<VulkanContext> context_;
    std::unique_ptr<ui::PaintResources> ui_resources_;
    ui::IUiPainter* ui_painter_ = nullptr;
    CmdSetView view_{};
    VulkanPost post_;

    VkCommandBuffer active_cmd_ = VK_NULL_HANDLE;
    VkRenderPass active_render_pass_ = VK_NULL_HANDLE;
    VkExtent2D pass_extent_{0, 0};
    std::uint32_t active_slot_ = 0;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    std::vector<CachedPipeline> pipelines_;
    std::array<FrameSlot, kMaxFrameSlots> frame_slots_{};

    std::shared_ptr<VulkanTexture> white_texture_;
    std::shared_ptr<VulkanTexture> white_texture_array_;
    std::shared_ptr<VulkanShader> particle_shader_;
    VkBuffer particle_quad_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory particle_quad_memory_ = VK_NULL_HANDLE;
    VkBuffer mesh_instance_dummy_ = VK_NULL_HANDLE;
    VkDeviceMemory mesh_instance_dummy_memory_ = VK_NULL_HANDLE;
};

} // namespace engine::render
