#pragma once

#include <engine/render/backend.h>

namespace engine::ui {
class IUiPainter;
}

namespace engine::render {

class OpenGLRenderBackend final : public IRenderBackend {
public:
    void set_ui_painter(ui::IUiPainter* painter);
    void execute(const CommandBuffer& commands) override;

private:
    ui::IUiPainter* ui_painter_ = nullptr;
};

}
