#include "vulkan_backend.h"

#include "ui/painter.h"
#include "ui/render/paint_resources.h"

#include <engine/render/command_buffer.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <utility>

namespace engine::render {

namespace {

struct ExecuteVisitor {
    ui::IUiPainter* painter = nullptr;
    VulkanRenderBackend* backend = nullptr;

    void operator()(const CmdDrawMesh&) const {
        // Stage 4: bind graphics pipeline, push constants / descriptor sets, and record vkCmdDrawIndexed / vkCmdDraw.
    }

    void operator()(const CmdDrawUI& cmd) const {
        if (painter == nullptr || cmd.document == nullptr) {
            return;
        }
        ui::paint_document(
            *cmd.document,
            cmd.stylesheet,
            *painter,
            ui::UiPaintInput{
                .canvas_rect = cmd.rect,
                .pointer = cmd.pointer,
                .pointer_down = cmd.pointer_down,
                .delta_time = cmd.delta_time,
                .window_width = cmd.window_width,
                .window_height = cmd.window_height,
                .ui_offset = cmd.ui_offset,
                .ui_scale = cmd.ui_scale,
                .inspector_hover = cmd.inspector_hover,
                .inspector_selection = cmd.inspector_selection,
                .inspector_selection_path = cmd.inspector_selection_path,
                .inspector_selection_owner = cmd.inspector_selection_owner,
                .canvas = cmd.canvas,
                .popup_bounds = cmd.popup_bounds,
                .popup_layer = cmd.popup_layer,
            });
    }

    void operator()(const CmdDrawParticles&) const {
        // Stage 4: record instanced particle draw.
    }

    void operator()(const CmdSetView& cmd) const {
        backend->set_view(cmd);
    }
};

} // namespace

VulkanRenderBackend::VulkanRenderBackend(std::shared_ptr<VulkanContext> context)
    : context_(std::move(context))
    , ui_resources_(std::make_unique<ui::PaintResources>()) {}

VulkanRenderBackend::~VulkanRenderBackend() = default;

void VulkanRenderBackend::execute(const CommandBuffer& commands) {
    view_ = CmdSetView{};
    commands.execute(ExecuteVisitor{.painter = ui_painter_, .backend = this});
}

} // namespace engine::render
