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
// A cylinder along the Y axis centred on the origin, with `radius`, `height`, and `segments` (at least 3) around Y.
[[nodiscard]] MeshDesc make_cylinder(float radius = 0.5f, float height = 1.0f, int segments = 16);
// A cone along the Y axis centred on the origin (base at -height/2, apex at +height/2), with `radius`, `height`,
// and `segments` (at least 3) around Y.
[[nodiscard]] MeshDesc make_cone(float radius = 0.5f, float height = 1.0f, int segments = 16);
// A torus centred on the origin on the XZ plane with symmetry axis along +Y, with `ring_radius`, `tube_radius`,
// `ring_segments` around the main ring (at least 3), and `tube_segments` around the tube (at least 3).
[[nodiscard]] MeshDesc make_torus(
        float ring_radius = 1.0f, float tube_radius = 0.035f, int ring_segments = 32, int tube_segments = 8);

}
