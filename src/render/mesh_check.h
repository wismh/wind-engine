#pragma once

// docs/tech/modules/Render.md#meshes

#include <engine/render/graphic_factory.h>

#include <string>

namespace engine::render {

// Why `desc` cannot be drawn: no vertices, an index count that is not a multiple of three, or an index at or past the
// vertex count. Empty when it can.
[[nodiscard]] std::string mesh_desc_error(const MeshDesc& desc);

}
