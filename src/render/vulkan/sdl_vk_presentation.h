#pragma once

// docs/tech/modules/Render.md

#include "core/presentation.h"

#include <memory>

namespace engine {

[[nodiscard]] std::unique_ptr<IPresentation> make_sdl_vk_presentation();

} // namespace engine
