#pragma once

// docs/tech/modules/Render.md (Fluids)

#include <engine/render/material.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>

namespace engine::render {

class IMesh;
class ITexture;

// Draws the Fluid2D on the same entity as soft cards: one camera-facing-in-XY card per particle, `size` particle
// radii wide, overlapping into a continuous body of liquid. One CmdDrawParticles (ParticleDraw::Plane) per fluid,
// sorted with sprites by `layer` and `order_in_layer`, culled by the fluid's box. Without a texture or material the
// cards use a generated soft disc (a radial falloff) on the unlit shader. A card is `color`, blended toward
// `foam_color` as the particle's speed approaches `foam_speed` (0: no foam).
struct FluidRender2D {
    // Surface: a continuous surface with an edge, refraction of what is behind and foam (CmdDrawFluidSurface; the
    // scene renders offscreen). Cards: only the soft cards, cheaper, no offscreen pass.
    enum class Look : std::uint8_t { Surface, Cards };
    Look look = Look::Surface;
    std::shared_ptr<ITexture> texture;    // the card image; null: the soft disc
    std::shared_ptr<IMaterial> material;  // wins over `texture`
    std::shared_ptr<IMesh> mesh;          // the card; null: builtin::mesh_quad
    glm::vec4 color{0.25f, 0.55f, 1.0f, 0.85f};
    glm::vec4 foam_color{0.9f, 0.95f, 1.0f, 1.0f};
    float foam_speed = 0.0f;
    float size = 4.0f;  // card width in particle radii
    float z = 0.0f;     // world z of the cards
    BlendMode blend = BlendMode::Alpha;
    int layer = 0;
    int order_in_layer = 0;
    bool frustum_cull = true;
    // Surface only: each particle adds `splat_weight` of density at its center (falling to 0 at the card's edge, so
    // packed water sums to about 1); the surface is where the density passes `threshold`, over `edge` either side.
    // `refraction` is how far, in pixels, what is behind shifts at the edge (the inside is flat); `foam_scale` scales
    // the foam share.
    float splat_weight = 1.0f;
    float threshold = 0.5f;
    float edge = 0.1f;
    float refraction = 6.0f;
    float foam_scale = 2.0f;
};

}
