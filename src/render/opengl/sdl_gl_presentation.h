#pragma once

#include "window_manager.h"

#include "core/presentation.h"

#include <memory>

namespace engine {

[[nodiscard]] std::unique_ptr<IPresentation> make_sdl_presentation(
    std::shared_ptr<render::IGraphicFactory> factory,
    std::shared_ptr<render::IRenderBackend> backend,
    WindowManager::CanvasFactory make_canvas);

[[nodiscard]] std::unique_ptr<IPresentation> make_sdl_gl_presentation();

}
