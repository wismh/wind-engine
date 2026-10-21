#pragma once

// docs/tech/modules/Render.md

#include "vulkan_backend.h"
#include "vulkan_context.h"

#include "render/opengl/window_canvas.h"
#include "render/opengl/window_system.h"
#include "ui/render/ui_device.h"
#include "ui/render/ui_renderer.h"

#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine::render {

class VulkanCanvas final : public IWindowCanvas {
public:
    static constexpr std::uint32_t kMaxFramesInFlight = 2;

    VulkanCanvas(WindowSystem& window, CommandBuffer& commands, VulkanRenderBackend& backend);
    ~VulkanCanvas() override;

    VulkanCanvas(const VulkanCanvas&) = delete;
    VulkanCanvas& operator=(const VulkanCanvas&) = delete;

    [[nodiscard]] SDL_WindowFlags prepare_window(const WindowDesc& desc) override;
    [[nodiscard]] bool init(bool with_ui_painter = true) override;
    [[nodiscard]] bool load_ui_font(const Font& font) override;
    [[nodiscard]] bool add_font(AssetId id, const Font& font) override;
    [[nodiscard]] bool add_image(AssetId id, const TextureDesc& desc) override;
    [[nodiscard]] bool reset_ui_painter() override;
    void make_current() override;
    [[nodiscard]] bool set_vsync(bool on) override;
    [[nodiscard]] ui::IUiPainter* ui_painter() const noexcept override { return ui_renderer_.get(); }

    void draw() override;
    void render() override;
    [[nodiscard]] TextureDesc read_pixels() override;
    void present() override;

    void shutdown();

private:
    [[nodiscard]] bool create_sync_and_commands();
    void destroy_sync_and_commands();
    [[nodiscard]] bool recreate_swapchain();
    void destroy_swapchain();
    [[nodiscard]] bool create_depth_resources(VkExtent2D extent);
    void destroy_depth_resources();
    [[nodiscard]] bool create_render_pass();
    [[nodiscard]] bool create_framebuffers();

    WindowSystem* window_ = nullptr;
    CommandBuffer* commands_ = nullptr;
    VulkanRenderBackend* backend_ = nullptr;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchain_extent_{0, 0};
    bool swapchain_supports_transfer_src_ = false;
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;

    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> command_buffers_{};
    std::array<VkSemaphore, kMaxFramesInFlight> image_available_semaphores_{};
    std::array<VkSemaphore, kMaxFramesInFlight> render_finished_semaphores_{};
    std::array<VkFence, kMaxFramesInFlight> in_flight_fences_{};

    std::uint32_t current_frame_ = 0;
    std::uint32_t acquired_image_index_ = 0;
    bool frame_rendered_ = false;
    bool swapchain_dirty_ = false;
    bool vsync_ = true;

    std::unique_ptr<ui::IUiDevice> ui_device_;
    std::unique_ptr<ui::UiRenderer> ui_renderer_;
};

} // namespace engine::render
