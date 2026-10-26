#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"

#include <engine/render/backend.h>
#include <engine/render/commands.h>

#include <memory>

namespace engine::ui {
class IUiPainter;
struct PaintResources;
} // namespace engine::ui

namespace engine::render {

class VulkanRenderBackend final : public IRenderBackend {
public:
    explicit VulkanRenderBackend(std::shared_ptr<VulkanContext> context);
    ~VulkanRenderBackend() override;

    VulkanRenderBackend(const VulkanRenderBackend&) = delete;
    VulkanRenderBackend& operator=(const VulkanRenderBackend&) = delete;

    void set_ui_painter(ui::IUiPainter* painter) noexcept { ui_painter_ = painter; }
    [[nodiscard]] ui::PaintResources& ui_resources() noexcept { return *ui_resources_; }
    [[nodiscard]] VulkanContext& context() noexcept { return *context_; }
    [[nodiscard]] const VulkanContext& context() const noexcept { return *context_; }
    [[nodiscard]] const CmdSetView& last_view() const noexcept { return view_; }

    void set_view(const CmdSetView& view) noexcept { view_ = view; }
    void execute(const CommandBuffer& commands) override;

private:
    std::shared_ptr<VulkanContext> context_;
    std::unique_ptr<ui::PaintResources> ui_resources_;
    ui::IUiPainter* ui_painter_ = nullptr;
    CmdSetView view_{};
};

} // namespace engine::render
