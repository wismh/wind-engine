#pragma once

#include "core/presentation.h"

#include <memory>

namespace engine {

[[nodiscard]] std::unique_ptr<IPresentation> make_sdl_gl_presentation();

}
