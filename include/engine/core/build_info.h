#pragma once

// docs/tech/build/CMake.md

#include <engine/build_id.h>
#include <engine/core/export.h>

#include <string_view>

namespace engine {

// kBuildId as compiled into the engine library. In the editor process every module reaches the one
// engine.dll, so a game module compares its own baked kBuildId against this value.
[[nodiscard]] ENGINE_API std::string_view build_id() noexcept;

}
