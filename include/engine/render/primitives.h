#pragma once

// docs/tech/modules/Render.md#primitives

#include <engine/render/graphic_factory.h>

namespace engine::render {

// Procedural meshes centred on the origin, for IGraphicFactory::create_mesh. Indexed, counter-clockwise front faces,
// outward unit normals, UVs in [0, 1].

// The unit quad of builtin::mesh_quad: XY plane, +Z normal, 4 vertices, 2 triangles.
[[nodiscard]] MeshDesc make_quad();
// An axis-aligned cube with edge `size`: 4 vertices per face so each face has its own normal and full 0..1 UVs.
[[nodiscard]] MeshDesc make_cube(float size = 1.0f);
// A square on the XZ plane with edge `size`, +Y normal, split into `subdivisions` (at least 1) cells per side. U runs
// along +X and V along -Z.
[[nodiscard]] MeshDesc make_plane(float size = 1.0f, int subdivisions = 1);
// A UV sphere: `segments` (at least 3) around Y and `rings` (at least 2) from pole to pole. U wraps around Y from +Z
// towards +X; V is 1 at the top pole and 0 at the bottom. The seam column is duplicated.
[[nodiscard]] MeshDesc make_uv_sphere(float radius = 0.5f, int segments = 32, int rings = 16);

}
