#include <engine/ecs/world_serializer.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/prototype_asset.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/model.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/pointer_target.h>
#include <engine/ecs/transform.h>
#include <engine/render/ambient_light.h>
#include <engine/render/point_light.h>
#include <engine/render/spot_light.h>
#include <engine/render/animation.h>
#include <engine/render/directional_light.h>
#include <engine/render/fluid_render_2d.h>
#include <engine/render/fog.h>
#include <engine/render/post_process.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/decal.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/render/tilemap.h>
#include <engine/resources/assets_db.h>

#include "core/base64.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ecs {
namespace {

// Preserves the asset GUIDs used to populate GPU/clip pointers on an entity so `save_world_to_toml` can round-trip
// `Sprite`, `Renderable`, and `SpriteAnimator` cleanly.
struct SerializedAssetRefs {
    AssetId sprite_texture;
    std::string sprite_name;
    AssetId sprite_mesh;
    AssetId sprite_material;
    AssetId renderable_mesh;
    AssetId renderable_material;
    AssetId animator_clip;
    AssetId tilemap;
    AssetId decal_mesh;
    AssetId decal_material;
    AssetId decal_texture;
    AssetId fluid_texture;
    AssetId fluid_material;
};

[[nodiscard]] bool has_asset(const AssetId& id) noexcept {
    return id != AssetId{};
}

[[nodiscard]] std::string_view body_type_name(BodyType type) {
    switch (type) {
    case BodyType::Static:
        return "static";
    case BodyType::Kinematic:
        return "kinematic";
    case BodyType::Dynamic:
        return "dynamic";
    }
    return "kinematic";
}

void read_body_type(ComponentReader& in, BodyType& type, std::string_view component) {
    const auto it = in.table().find("type");
    if (it == in.table().end()) {
        return;
    }
    if (const std::string* s = it->second.as_string()) {
        if (*s == "static") {
            type = BodyType::Static;
        } else if (*s == "kinematic") {
            type = BodyType::Kinematic;
        } else if (*s == "dynamic") {
            type = BodyType::Dynamic;
        } else {
            in.fail(std::format("invalid {} type \"{}\"", component, *s));
        }
    } else if (const std::int64_t* i = it->second.as_int()) {
        if (*i == 0) {
            type = BodyType::Static;
        } else if (*i == 1) {
            type = BodyType::Kinematic;
        } else if (*i == 2) {
            type = BodyType::Dynamic;
        } else {
            in.fail(std::format("invalid {} type index", component));
        }
    } else {
        in.fail("invalid field \"type\"");
    }
}

template<typename T>
bool load_asset_or_validate_headless(
        AssetsDb& db, AssetId id, ImporterKind expected_kind, std::shared_ptr<T>& out, ComponentReader& reader,
        std::string_view field_name) {
    auto res = db.try_get<T>(id);
    if (res) {
        out = std::move(*res);
        return true;
    }
    if (res.error() == AssetError::NotReady) {
        const CatalogEntry* entry = db.catalog().find(id);
        if (entry != nullptr && entry->importer == expected_kind) {
            return true;
        }
    }
    reader.fail(std::format("asset error on field \"{}\" ({}): {}", field_name, id.hex(), to_string(res.error())));
    return false;
}

namespace {

// Fluid2D: a region as a nested table: shape "box" (center, half_extents) or "circle" (center, radius).
void write_fluid_region(ComponentWriter& out, const FluidRegion2D& region) {
    out.field("shape", std::string{region.shape == FluidRegion2D::Shape::Circle ? "circle" : "box"});
    out.field("center", region.center);
    if (region.shape == FluidRegion2D::Shape::Circle) {
        out.field("radius", region.radius);
    } else {
        out.field("half_extents", region.half_extents);
    }
}

void read_fluid_region(ComponentReader& in, FluidRegion2D& region) {
    std::string shape = "box";
    (void)in.field("shape", shape);
    if (shape == "circle") {
        region.shape = FluidRegion2D::Shape::Circle;
    } else if (shape == "box") {
        region.shape = FluidRegion2D::Shape::Box;
    } else {
        in.fail("fluid_2d region shape \"" + shape + "\" is not box or circle");
        return;
    }
    (void)in.field("center", region.center);
    (void)in.field("half_extents", region.half_extents);
    (void)in.field("radius", region.radius);
}

// Writes `items` as an array of nested tables under `name`, one `write(nested, item)` each.
template <class List, class Write>
void write_fluid_list(ComponentWriter& out, std::string_view name, const List& items, const Write& write) {
    if (items.empty()) {
        return;
    }
    FieldArray array;
    array.reserve(items.size());
    for (const auto& item : items) {
        FieldTable table;
        ComponentWriter nested = out.nested(table);
        write(nested, item);
        array.push_back(FieldValue{std::move(table)});
    }
    out.table().insert_or_assign(std::string{name}, FieldValue{std::move(array)});
}

// Reads the array `name` (missing: empty) into `items`, one `read(nested, item)` per table.
template <class List, class Read>
void read_fluid_list(ComponentReader& in, std::string_view name, List& items, const Read& read) {
    items.clear();
    const auto found = in.table().find(std::string{name});
    if (found == in.table().end()) {
        return;
    }
    const FieldArray* array = found->second.as_array();
    if (array == nullptr) {
        in.fail("fluid_2d " + std::string{name} + " is not an array");
        return;
    }
    for (const FieldValue& element : *array) {
        const FieldTable* table = element.as_table();
        if (table == nullptr) {
            in.fail("fluid_2d " + std::string{name} + " entry is not a table");
            return;
        }
        ComponentReader nested = in.nested(*table);
        items.emplace_back();
        read(nested, items.back());
        if (!nested.ok()) {
            in.fail(std::string(nested.error()));
            return;
        }
    }
}

}

void register_builtin_components(
        WorldSerializer& serializer,
        std::map<std::string, WorldSerializer::Entry, std::less<>>& entries) {
    serializer.register_component<Transform>(
            "transform",
            [](ComponentWriter& out, const Transform& c) {
                out.field("position", c.position);
                out.field("rotation", c.rotation);
                out.field("scale", c.scale);
            },
            [](ComponentReader& in, Transform& c) {
                (void)in.field("position", c.position);
                if (in.table().contains("rotation")) {
                    (void)in.field("rotation", c.rotation);
                } else if (in.table().contains("rotation_z")) {
                    float radians = 0.0f;
                    if (in.field("rotation_z", radians)) {
                        c.rotation = rotation_z(radians);
                    }
                } else if (in.table().contains("euler_xyz")) {
                    glm::vec3 euler{0.0f};
                    if (in.field("euler_xyz", euler)) {
                        c.rotation = euler_xyz(euler);
                    }
                }
                (void)in.field("scale", c.scale);
            });

    serializer.register_component<Camera>(
            "camera",
            [](ComponentWriter& out, const Camera& c) {
                out.field(
                        "projection",
                        c.projection == Projection::Perspective ? std::string_view("perspective")
                                                                : std::string_view("orthographic"));
                out.field("ortho_size", c.ortho_size);
                out.field("fov_y", c.fov_y);
                out.field("auto_aspect", c.auto_aspect);
                out.field("aspect", c.aspect);
                out.field("near_clip", c.near_clip);
                out.field("far_clip", c.far_clip);
            },
            [](ComponentReader& in, Camera& c) {
                if (const auto it = in.table().find("projection"); it != in.table().end()) {
                    if (const std::string* s = it->second.as_string()) {
                        if (*s == "orthographic") {
                            c.projection = Projection::Orthographic;
                        } else if (*s == "perspective") {
                            c.projection = Projection::Perspective;
                        } else {
                            in.fail("invalid camera projection \"" + *s + "\"");
                        }
                    } else if (const std::int64_t* i = it->second.as_int()) {
                        if (*i == 0) {
                            c.projection = Projection::Orthographic;
                        } else if (*i == 1) {
                            c.projection = Projection::Perspective;
                        } else {
                            in.fail("invalid camera projection index");
                        }
                    } else {
                        in.fail("invalid field \"projection\"");
                    }
                }
                (void)in.field("ortho_size", c.ortho_size);
                (void)in.field("fov_y", c.fov_y);
                (void)in.field("auto_aspect", c.auto_aspect);
                (void)in.field("aspect", c.aspect);
                (void)in.field("near_clip", c.near_clip);
                (void)in.field("far_clip", c.far_clip);
            });

    {
        WorldSerializer::Entry sprite_entry;
        sprite_entry.name = "sprite";
        sprite_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const render::Sprite* s = world.try_get<render::Sprite>(entity);
            if (s == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->sprite_texture)) {
                    out.field("texture", refs->sprite_texture);
                }
                if (!refs->sprite_name.empty()) {
                    out.field("sprite", refs->sprite_name);
                }
                if (has_asset(refs->sprite_mesh)) {
                    out.field("mesh", refs->sprite_mesh);
                }
                if (has_asset(refs->sprite_material)) {
                    out.field("material", refs->sprite_material);
                }
            }
            out.field("color", s->color);
            out.field("layer", s->layer);
            out.field("order_in_layer", s->order_in_layer);
            out.field("flip_x", s->flip_x);
            out.field("flip_y", s->flip_y);
            out.field("tiling", s->tiling);
            out.field("offset", s->offset);
            out.field("pixel_size", s->pixel_size);
            out.field("pixels_per_unit", s->pixels_per_unit);
            out.field("pivot", s->pivot);
            out.field("frustum_cull", s->frustum_cull);
            return true;
        };
        sprite_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::Sprite s{};
            AssetId texture_id;
            std::string sprite_name;
            AssetId mesh_id;
            AssetId material_id;
            (void)in.field("texture", texture_id);
            (void)in.field("sprite", sprite_name);
            (void)in.field("mesh", mesh_id);
            (void)in.field("material", material_id);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(texture_id) && in.assets() != nullptr) {
                auto loaded = in.assets()->try_get_sprite(texture_id, sprite_name);
                if (loaded) {
                    s = std::move(*loaded);
                } else if (loaded.error() == AssetError::NotReady) {
                    const CatalogEntry* entry = in.assets()->catalog().find(texture_id);
                    if (entry == nullptr ||
                            (entry->importer != ImporterKind::Texture && entry->importer != ImporterKind::UiImage)) {
                        in.fail("sprite texture is not a texture asset");
                        return false;
                    }
                    s.pixels_per_unit = entry->texture.pixels_per_unit;
                } else {
                    in.fail(std::format(
                            "sprite asset error ({}): {}", texture_id.hex(), to_string(loaded.error())));
                    return false;
                }
            }
            if (has_asset(mesh_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), mesh_id, ImporterKind::Mesh, s.mesh, in, "mesh")) {
                    return false;
                }
            }
            if (has_asset(material_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), material_id, ImporterKind::Material, s.material, in, "material")) {
                    return false;
                }
            }
            (void)in.field("color", s.color);
            (void)in.field("layer", s.layer);
            (void)in.field("order_in_layer", s.order_in_layer);
            (void)in.field("flip_x", s.flip_x);
            (void)in.field("flip_y", s.flip_y);
            (void)in.field("tiling", s.tiling);
            (void)in.field("offset", s.offset);
            (void)in.field("pixel_size", s.pixel_size);
            (void)in.field("pixels_per_unit", s.pixels_per_unit);
            (void)in.field("pivot", s.pivot);
            (void)in.field("frustum_cull", s.frustum_cull);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(texture_id) || !sprite_name.empty() || has_asset(mesh_id) || has_asset(material_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->sprite_texture = texture_id;
                refs->sprite_name = std::move(sprite_name);
                refs->sprite_mesh = mesh_id;
                refs->sprite_material = material_id;
            }
            if (render::Sprite* existing = world.try_get<render::Sprite>(entity)) {
                *existing = std::move(s);
            } else {
                world.emplace<render::Sprite>(entity, std::move(s));
            }
            return true;
        };
        sprite_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::Sprite>(entity) == nullptr) {
                return false;
            }
            world.remove<render::Sprite>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->sprite_texture = {};
                refs->sprite_name.clear();
                refs->sprite_mesh = {};
                refs->sprite_material = {};
            }
            return true;
        };
        entries.emplace("sprite", std::move(sprite_entry));
    }

    {
        WorldSerializer::Entry anim_entry;
        anim_entry.name = "sprite_animator";
        anim_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const render::SpriteAnimator* a = world.try_get<render::SpriteAnimator>(entity);
            if (a == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->animator_clip)) {
                    out.field("clip", refs->animator_clip);
                }
            }
            out.field("speed", a->speed);
            out.field("playing", a->playing);
            out.field("current_frame", a->current_frame);
            return true;
        };
        anim_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::SpriteAnimator a{};
            AssetId clip_id;
            (void)in.field("clip", clip_id);
            (void)in.field("speed", a.speed);
            (void)in.field("playing", a.playing);
            (void)in.field("current_frame", a.current_frame);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(clip_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), clip_id, ImporterKind::Animation, a.clip, in, "clip")) {
                    return false;
                }
            }
            if (has_asset(clip_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->animator_clip = clip_id;
            }
            if (render::SpriteAnimator* existing = world.try_get<render::SpriteAnimator>(entity)) {
                *existing = std::move(a);
            } else {
                world.emplace<render::SpriteAnimator>(entity, std::move(a));
            }
            return true;
        };
        anim_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::SpriteAnimator>(entity) == nullptr) {
                return false;
            }
            world.remove<render::SpriteAnimator>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->animator_clip = {};
            }
            return true;
        };
        entries.emplace("sprite_animator", std::move(anim_entry));
    }

    {
        WorldSerializer::Entry renderable_entry;
        renderable_entry.name = "renderable";
        renderable_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const render::Renderable* r = world.try_get<render::Renderable>(entity);
            if (r == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->renderable_mesh)) {
                    out.field("mesh", refs->renderable_mesh);
                }
                if (has_asset(refs->renderable_material)) {
                    out.field("material", refs->renderable_material);
                }
            }
            out.field("color", r->color);
            out.field("layer", r->layer);
            out.field("order_in_layer", r->order_in_layer);
            out.field("frustum_cull", r->frustum_cull);
            return true;
        };
        renderable_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::Renderable r{};
            AssetId mesh_id;
            AssetId material_id;
            (void)in.field("mesh", mesh_id);
            (void)in.field("material", material_id);
            (void)in.field("color", r.color);
            (void)in.field("layer", r.layer);
            (void)in.field("order_in_layer", r.order_in_layer);
            (void)in.field("frustum_cull", r.frustum_cull);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(mesh_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), mesh_id, ImporterKind::Mesh, r.mesh, in, "mesh")) {
                    return false;
                }
            }
            if (has_asset(material_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), material_id, ImporterKind::Material, r.material, in, "material")) {
                    return false;
                }
            }
            if (has_asset(mesh_id) || has_asset(material_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->renderable_mesh = mesh_id;
                refs->renderable_material = material_id;
            }
            if (render::Renderable* existing = world.try_get<render::Renderable>(entity)) {
                *existing = std::move(r);
            } else {
                world.emplace<render::Renderable>(entity, std::move(r));
            }
            return true;
        };
        renderable_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::Renderable>(entity) == nullptr) {
                return false;
            }
            world.remove<render::Renderable>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->renderable_mesh = {};
                refs->renderable_material = {};
            }
            return true;
        };
        entries.emplace("renderable", std::move(renderable_entry));
    }

    {
        WorldSerializer::Entry decal_entry;
        decal_entry.name = "decal";
        decal_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const auto* d = world.try_get<render::Decal>(entity);
            if (d == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->decal_mesh)) {
                    out.field("mesh", refs->decal_mesh);
                }
                if (has_asset(refs->decal_material)) {
                    out.field("material", refs->decal_material);
                }
                if (has_asset(refs->decal_texture)) {
                    out.field("texture", refs->decal_texture);
                }
            }
            out.field("color", d->color);
            out.field("uv_scale", d->uv_scale);
            out.field("uv_offset", d->uv_offset);
            out.field("layer", d->layer);
            out.field("order_in_layer", d->order_in_layer);
            out.field("angle_cutoff", d->angle_cutoff);
            out.field("normal_fade", d->normal_fade);
            out.field("frustum_cull", d->frustum_cull);
            return true;
        };
        decal_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::Decal d{};
            AssetId mesh_id;
            AssetId material_id;
            AssetId texture_id;
            (void)in.field("mesh", mesh_id);
            (void)in.field("material", material_id);
            (void)in.field("texture", texture_id);
            (void)in.field("color", d.color);
            (void)in.field("uv_scale", d.uv_scale);
            (void)in.field("uv_offset", d.uv_offset);
            (void)in.field("layer", d.layer);
            (void)in.field("order_in_layer", d.order_in_layer);
            (void)in.field("angle_cutoff", d.angle_cutoff);
            (void)in.field("normal_fade", d.normal_fade);
            (void)in.field("frustum_cull", d.frustum_cull);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(mesh_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), mesh_id, ImporterKind::Mesh, d.mesh, in, "mesh")) {
                    return false;
                }
            }
            if (has_asset(material_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), material_id, ImporterKind::Material, d.material, in, "material")) {
                    return false;
                }
            }
            if (has_asset(texture_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), texture_id, ImporterKind::Texture, d.texture, in, "texture")) {
                    return false;
                }
            }
            if (has_asset(mesh_id) || has_asset(material_id) || has_asset(texture_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->decal_mesh = mesh_id;
                refs->decal_material = material_id;
                refs->decal_texture = texture_id;
            }
            if (render::Decal* existing = world.try_get<render::Decal>(entity)) {
                *existing = std::move(d);
            } else {
                world.emplace<render::Decal>(entity, std::move(d));
            }
            return true;
        };
        decal_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::Decal>(entity) == nullptr) {
                return false;
            }
            world.remove<render::Decal>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->decal_mesh = {};
                refs->decal_material = {};
                refs->decal_texture = {};
            }
            return true;
        };
        entries.emplace("decal", std::move(decal_entry));
    }

    {
        // FluidRender2D: the look of a Fluid2D on the same entity. `look` is "surface" or "cards"; `blend` is
        // "opaque", "alpha" or "additive".
        WorldSerializer::Entry fluid_entry;
        fluid_entry.name = "fluid_render_2d";
        fluid_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const auto* f = world.try_get<render::FluidRender2D>(entity);
            if (f == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->fluid_texture)) {
                    out.field("texture", refs->fluid_texture);
                }
                if (has_asset(refs->fluid_material)) {
                    out.field("material", refs->fluid_material);
                }
            }
            out.field("look", std::string{f->look == render::FluidRender2D::Look::Cards ? "cards" : "surface"});
            out.field("color", f->color);
            out.field("foam_color", f->foam_color);
            out.field("foam_speed", f->foam_speed);
            out.field("size", f->size);
            out.field("z", f->z);
            out.field("blend", std::string{f->blend == render::BlendMode::Opaque     ? "opaque"
                                           : f->blend == render::BlendMode::Additive ? "additive"
                                                                                     : "alpha"});
            out.field("layer", f->layer);
            out.field("order_in_layer", f->order_in_layer);
            out.field("frustum_cull", f->frustum_cull);
            out.field("splat_weight", f->splat_weight);
            out.field("threshold", f->threshold);
            out.field("edge", f->edge);
            out.field("refraction", f->refraction);
            out.field("foam_scale", f->foam_scale);
            return true;
        };
        fluid_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::FluidRender2D f{};
            AssetId texture_id;
            AssetId material_id;
            std::string blend = "alpha";
            std::string look = "surface";
            (void)in.field("look", look);
            (void)in.field("texture", texture_id);
            (void)in.field("material", material_id);
            (void)in.field("color", f.color);
            (void)in.field("foam_color", f.foam_color);
            (void)in.field("foam_speed", f.foam_speed);
            (void)in.field("size", f.size);
            (void)in.field("z", f.z);
            (void)in.field("blend", blend);
            (void)in.field("layer", f.layer);
            (void)in.field("order_in_layer", f.order_in_layer);
            (void)in.field("frustum_cull", f.frustum_cull);
            (void)in.field("splat_weight", f.splat_weight);
            (void)in.field("threshold", f.threshold);
            (void)in.field("edge", f.edge);
            (void)in.field("refraction", f.refraction);
            (void)in.field("foam_scale", f.foam_scale);
            if (look == "cards") {
                f.look = render::FluidRender2D::Look::Cards;
            } else if (look != "surface") {
                in.fail("fluid_render_2d look \"" + look + "\" is not surface or cards");
            }
            if (blend == "opaque") {
                f.blend = render::BlendMode::Opaque;
            } else if (blend == "additive") {
                f.blend = render::BlendMode::Additive;
            } else if (blend == "alpha") {
                f.blend = render::BlendMode::Alpha;
            } else {
                in.fail("fluid_render_2d blend \"" + blend + "\" is not opaque, alpha or additive");
            }
            if (!in.ok()) {
                return false;
            }
            if (has_asset(texture_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), texture_id, ImporterKind::Texture, f.texture, in, "texture")) {
                    return false;
                }
            }
            if (has_asset(material_id) && in.assets() != nullptr) {
                if (!load_asset_or_validate_headless(
                            *in.assets(), material_id, ImporterKind::Material, f.material, in, "material")) {
                    return false;
                }
            }
            if (has_asset(texture_id) || has_asset(material_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->fluid_texture = texture_id;
                refs->fluid_material = material_id;
            }
            if (render::FluidRender2D* existing = world.try_get<render::FluidRender2D>(entity)) {
                *existing = std::move(f);
            } else {
                world.emplace<render::FluidRender2D>(entity, std::move(f));
            }
            return true;
        };
        fluid_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::FluidRender2D>(entity) == nullptr) {
                return false;
            }
            world.remove<render::FluidRender2D>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->fluid_texture = {};
                refs->fluid_material = {};
            }
            return true;
        };
        entries.emplace("fluid_render_2d", std::move(fluid_entry));
    }

    {
        WorldSerializer::Entry tilemap_entry;
        tilemap_entry.name = "tilemap";
        tilemap_entry.write = [](const World& world, Entity entity, ComponentWriter& out) -> bool {
            const render::Tilemap* t = world.try_get<render::Tilemap>(entity);
            if (t == nullptr) {
                return false;
            }
            if (const SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                if (has_asset(refs->tilemap)) {
                    out.field("tilemap", refs->tilemap);
                }
            }
            out.field("color", t->color);
            out.field("layer", t->layer);
            out.field("order_in_layer", t->order_in_layer);
            return true;
        };
        tilemap_entry.read = [](World& world, Entity entity, ComponentReader& in) -> bool {
            render::Tilemap t{};
            AssetId tilemap_id;
            (void)in.field("tilemap", tilemap_id);
            (void)in.field("color", t.color);
            (void)in.field("layer", t.layer);
            (void)in.field("order_in_layer", t.order_in_layer);
            if (!in.ok()) {
                return false;
            }
            if (has_asset(tilemap_id) && in.assets() != nullptr) {
                std::shared_ptr<render::TilemapAsset> asset;
                if (!load_asset_or_validate_headless(
                            *in.assets(), tilemap_id, ImporterKind::Tilemap, asset, in, "tilemap")) {
                    return false;
                }
                if (asset != nullptr) {
                    t.layers = asset->desc.layers;
                    t.asset = std::move(asset);
                }
            }
            if (has_asset(tilemap_id)) {
                SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity);
                if (refs == nullptr) {
                    refs = &world.emplace<SerializedAssetRefs>(entity);
                }
                refs->tilemap = tilemap_id;
            }
            if (render::Tilemap* existing = world.try_get<render::Tilemap>(entity)) {
                *existing = std::move(t);
            } else {
                world.emplace<render::Tilemap>(entity, std::move(t));
            }
            return true;
        };
        tilemap_entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<render::Tilemap>(entity) == nullptr) {
                return false;
            }
            world.remove<render::Tilemap>(entity);
            if (SerializedAssetRefs* refs = world.try_get<SerializedAssetRefs>(entity)) {
                refs->tilemap = {};
            }
            return true;
        };
        entries.emplace("tilemap", std::move(tilemap_entry));
    }

    serializer.register_component<render::DirectionalLight>(
            "directional_light",
            [](ComponentWriter& out, const render::DirectionalLight& c) {
                out.field("color", c.color);
                out.field("intensity", c.intensity);
            },
            [](ComponentReader& in, render::DirectionalLight& c) {
                (void)in.field("color", c.color);
                (void)in.field("intensity", c.intensity);
            });

    serializer.register_component<render::AmbientLight>(
            "ambient_light",
            [](ComponentWriter& out, const render::AmbientLight& c) {
                out.field("color", c.color);
                out.field("intensity", c.intensity);
            },
            [](ComponentReader& in, render::AmbientLight& c) {
                (void)in.field("color", c.color);
                (void)in.field("intensity", c.intensity);
            });

    serializer.register_component<render::PointLight>(
            "point_light",
            [](ComponentWriter& out, const render::PointLight& c) {
                out.field("color", c.color);
                out.field("intensity", c.intensity);
                out.field("range", c.range);
            },
            [](ComponentReader& in, render::PointLight& c) {
                (void)in.field("color", c.color);
                (void)in.field("intensity", c.intensity);
                (void)in.field("range", c.range);
            });

    serializer.register_component<render::SpotLight>(
            "spot_light",
            [](ComponentWriter& out, const render::SpotLight& c) {
                out.field("color", c.color);
                out.field("intensity", c.intensity);
                out.field("range", c.range);
                out.field("inner_angle", c.inner_angle);
                out.field("outer_angle", c.outer_angle);
            },
            [](ComponentReader& in, render::SpotLight& c) {
                (void)in.field("color", c.color);
                (void)in.field("intensity", c.intensity);
                (void)in.field("range", c.range);
                (void)in.field("inner_angle", c.inner_angle);
                (void)in.field("outer_angle", c.outer_angle);
            });

    serializer.register_component<render::PostStack>(
            "post_stack",
            [](ComponentWriter& out, const render::PostStack& component) {
                out.field("hdr", component.hdr);
                FieldArray effects;
                effects.reserve(component.effects.size());
                for (const render::PostEffect& effect : component.effects) {
                    FieldTable table;
                    ComponentWriter nested = out.nested(table);
                    nested.field("shader", effect.shader);
                    nested.field("params", effect.params);
                    nested.field("sample_depth", effect.sample_depth);
                    effects.push_back(FieldValue{std::move(table)});
                }
                out.table().insert_or_assign("effects", FieldValue{std::move(effects)});
            },
            [](ComponentReader& in, render::PostStack& component) {
                (void)in.field("hdr", component.hdr);
                component.effects.clear();
                const auto found = in.table().find("effects");
                if (found == in.table().end()) {
                    return;
                }
                const FieldArray* effects = found->second.as_array();
                if (effects == nullptr) {
                    in.fail("post_stack effects is not an array");
                    return;
                }
                component.effects.reserve(effects->size());
                for (const FieldValue& element : *effects) {
                    const FieldTable* table = element.as_table();
                    if (table == nullptr) {
                        in.fail("post_stack effect is not a table");
                        return;
                    }
                    ComponentReader nested = in.nested(*table);
                    render::PostEffect effect;
                    (void)nested.field("shader", effect.shader);
                    (void)nested.field("params", effect.params);
                    (void)nested.field("sample_depth", effect.sample_depth);
                    if (!nested.ok()) {
                        in.fail(std::string(nested.error()));
                        return;
                    }
                    component.effects.push_back(effect);
                }
            });

    serializer.register_component<render::Fog>(
            "fog",
            [](ComponentWriter& out, const render::Fog& c) {
                out.field("color", c.color);
                out.field("start", c.start);
                out.field("end", c.end);
            },
            [](ComponentReader& in, render::Fog& c) {
                (void)in.field("color", c.color);
                (void)in.field("start", c.start);
                (void)in.field("end", c.end);
            });

    serializer.register_component<RigidBody2D>(
            "rigid_body_2d",
            [](ComponentWriter& out, const RigidBody2D& c) {
                out.field("velocity", c.velocity);
                out.field("type", body_type_name(c.type));
                out.field("mass", c.mass);
                out.field("gravity_scale", c.gravity_scale);
                out.field("angular_velocity", c.angular_velocity);
                out.field("sleeping", c.sleeping);
            },
            [](ComponentReader& in, RigidBody2D& c) {
                (void)in.field("velocity", c.velocity);
                // Same shape as Camera.projection: a known string, a known index, or a fail. A missing key keeps
                // the defaults (kinematic, mass 1, gravity_scale 1).
                read_body_type(in, c.type, "rigid_body_2d");
                (void)in.field("mass", c.mass);
                (void)in.field("gravity_scale", c.gravity_scale);
                (void)in.field("angular_velocity", c.angular_velocity);
                (void)in.field("sleeping", c.sleeping);
            });

    serializer.register_component<BoxCollider2D>(
            "box_collider_2d",
            [](ComponentWriter& out, const BoxCollider2D& c) {
                out.field("size", c.size);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, BoxCollider2D& c) {
                (void)in.field("size", c.size);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<PolygonCollider2D>(
            "polygon_collider_2d",
            [](ComponentWriter& out, const PolygonCollider2D& c) {
                std::vector<glm::vec2> vertices;
                const int count = std::min(static_cast<int>(c.count), kPolygonCollider2DCapacity);
                vertices.reserve(static_cast<std::size_t>(count));
                for (int i = 0; i < count; ++i) {
                    vertices.push_back(c.vertices[i]);
                }
                out.field("vertices", vertices);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, PolygonCollider2D& c) {
                std::vector<glm::vec2> vertices;
                if (!in.field("vertices", vertices)) {
                    return;
                }
                if (vertices.size() > static_cast<std::size_t>(kPolygonCollider2DCapacity)) {
                    in.fail("polygon_collider_2d has more than 8 vertices");
                    return;
                }
                c.count = static_cast<std::uint8_t>(vertices.size());
                for (std::size_t i = 0; i < vertices.size(); ++i) {
                    c.vertices[i] = vertices[i];
                }
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<CompoundCollider2D>(
            "compound_collider_2d",
            [](ComponentWriter& out, const CompoundCollider2D& c) {
                FieldArray shapes;
                shapes.reserve(c.shapes.size());
                for (const CompoundShape2D& shape : c.shapes) {
                    std::vector<glm::vec2> vertices;
                    const int count = std::min(static_cast<int>(shape.count), kPolygonCollider2DCapacity);
                    vertices.reserve(static_cast<std::size_t>(count));
                    for (int i = 0; i < count; ++i) {
                        vertices.push_back(shape.vertices[i]);
                    }
                    FieldTable table;
                    ComponentWriter nested = out.nested(table);
                    nested.field("vertices", vertices);
                    shapes.push_back(FieldValue{std::move(table)});
                }
                out.table().insert_or_assign("shapes", FieldValue{std::move(shapes)});
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, CompoundCollider2D& c) {
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
                c.shapes.clear();
                const auto found = in.table().find("shapes");
                if (found == in.table().end()) {
                    return;
                }
                const FieldArray* shapes = found->second.as_array();
                if (shapes == nullptr) {
                    in.fail("compound_collider_2d shapes is not an array");
                    return;
                }
                c.shapes.reserve(shapes->size());
                for (const FieldValue& element : *shapes) {
                    const FieldTable* table = element.as_table();
                    if (table == nullptr) {
                        in.fail("compound_collider_2d shape is not a table");
                        return;
                    }
                    ComponentReader nested = in.nested(*table);
                    std::vector<glm::vec2> vertices;
                    (void)nested.field("vertices", vertices);
                    if (!nested.ok()) {
                        in.fail(std::string(nested.error()));
                        return;
                    }
                    if (vertices.size() > static_cast<std::size_t>(kPolygonCollider2DCapacity)) {
                        in.fail("compound_collider_2d shape has more than 8 vertices");
                        return;
                    }
                    CompoundShape2D shape;
                    shape.count = static_cast<std::uint8_t>(vertices.size());
                    for (std::size_t i = 0; i < vertices.size(); ++i) {
                        shape.vertices[i] = vertices[i];
                    }
                    c.shapes.push_back(shape);
                }
            });

    serializer.register_component<SdfCollider2D>(
            "sdf_collider_2d",
            [](ComponentWriter& out, const SdfCollider2D& c) {
                const SdfField2D& field = c.field;
                out.field("cell_size", field.cell_size());
                out.field("band", field.band());
                out.field("origin", field.origin());
                out.field("outside", std::string_view{field.outside() == SdfOutside::Solid ? "solid" : "empty"});
                FieldArray chunks;
                const std::vector<glm::ivec2> coords = field.chunk_coords();
                chunks.reserve(coords.size());
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kSdfChunkSamples) * 2);
                for (const glm::ivec2 coord : coords) {
                    const std::span<const std::int16_t> samples = field.chunk_samples(coord);
                    for (std::size_t i = 0; i < samples.size(); ++i) {
                        const auto value = static_cast<std::uint16_t>(samples[i]);
                        bytes[2 * i] = static_cast<std::uint8_t>(value & 0xFFu);
                        bytes[2 * i + 1] = static_cast<std::uint8_t>(value >> 8);
                    }
                    FieldTable table;
                    ComponentWriter nested = out.nested(table);
                    nested.field("x", coord.x);
                    nested.field("y", coord.y);
                    nested.field("samples", base64_encode(bytes));
                    chunks.push_back(FieldValue{std::move(table)});
                }
                out.table().insert_or_assign("chunks", FieldValue{std::move(chunks)});
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, SdfCollider2D& c) {
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
                const FieldTable& table = in.table();
                if (!table.contains("cell_size") && !table.contains("band") && !table.contains("origin") &&
                    !table.contains("outside") && !table.contains("chunks")) {
                    return;
                }
                // Any field key rebuilds the whole field: missing description keys keep the current values.
                SdfField2DDesc desc{.cell_size = c.field.cell_size(),
                                    .band = c.field.band(),
                                    .origin = c.field.origin(),
                                    .outside = c.field.outside()};
                (void)in.field("cell_size", desc.cell_size);
                (void)in.field("band", desc.band);
                (void)in.field("origin", desc.origin);
                std::string outside;
                if (in.field("outside", outside) && !outside.empty()) {
                    if (outside == "empty") {
                        desc.outside = SdfOutside::Empty;
                    } else if (outside == "solid") {
                        desc.outside = SdfOutside::Solid;
                    } else {
                        in.fail("sdf_collider_2d outside is not \"empty\" or \"solid\"");
                        return;
                    }
                }
                SdfField2D field{desc};
                const auto found = table.find("chunks");
                if (found != table.end()) {
                    const FieldArray* chunks = found->second.as_array();
                    if (chunks == nullptr) {
                        in.fail("sdf_collider_2d chunks is not an array");
                        return;
                    }
                    std::vector<std::int16_t> samples(static_cast<std::size_t>(kSdfChunkSamples));
                    for (const FieldValue& element : *chunks) {
                        const FieldTable* chunk = element.as_table();
                        if (chunk == nullptr || !chunk->contains("x") || !chunk->contains("y") ||
                            !chunk->contains("samples")) {
                            in.fail("sdf_collider_2d chunk is not a table with x, y and samples");
                            return;
                        }
                        ComponentReader nested = in.nested(*chunk);
                        glm::ivec2 coord{0};
                        std::string text;
                        (void)nested.field("x", coord.x);
                        (void)nested.field("y", coord.y);
                        (void)nested.field("samples", text);
                        if (!nested.ok()) {
                            in.fail(std::string(nested.error()));
                            return;
                        }
                        const std::optional<std::vector<std::uint8_t>> bytes = base64_decode(text);
                        if (!bytes || bytes->size() != samples.size() * 2) {
                            in.fail("sdf_collider_2d chunk samples is not base64 of 1024 little-endian int16");
                            return;
                        }
                        for (std::size_t i = 0; i < samples.size(); ++i) {
                            const auto value = static_cast<std::uint16_t>((*bytes)[2 * i] | ((*bytes)[2 * i + 1] << 8));
                            samples[i] = static_cast<std::int16_t>(value);
                        }
                        (void)field.assign_chunk(coord, samples);
                    }
                }
                c.field = std::move(field);
            });

    // Parameters, sources, drains and sensors: the particles and the sources', drains' and sensors' state are
    // transient simulation state, neither saved nor replicated.
    serializer.register_component<Fluid2D>(
            "fluid_2d",
            [](ComponentWriter& out, const Fluid2D& c) {
                out.field("radius", c.params.radius);
                out.field("smoothing_length", c.params.smoothing_length);
                out.field("rest_density", c.params.rest_density);
                out.field("viscosity", c.params.viscosity);
                out.field("relaxation", c.params.relaxation);
                out.field("tensile_strength", c.params.tensile_strength);
                out.field("iterations", c.params.iterations);
                out.field("substeps", c.params.substeps);
                out.field("max_particles", c.params.max_particles);
                out.field("gravity_scale", c.params.gravity_scale);
                out.field("friction", c.params.friction);
                out.field("restitution", c.params.restitution);
                out.field("density", c.params.density);
                out.field("cohesion", c.params.cohesion);
                out.field("vorticity", c.params.vorticity);
                out.field("layer", c.params.layer);
                out.field("mask", c.params.mask);
                out.field("sleep", c.params.sleep);
                out.field("sleep_speed", c.params.sleep_speed);
                out.field("sleep_ticks", c.params.sleep_ticks);
                out.field("wake_speed", c.params.wake_speed);
                write_fluid_list(out, "sources", c.sources, [](ComponentWriter& w, const FluidSource2D& source) {
                    write_fluid_region(w, source.region);
                    w.field("rate", source.rate);
                    w.field("velocity", source.velocity);
                    w.field("enabled", source.enabled);
                });
                write_fluid_list(out, "drains", c.drains, [](ComponentWriter& w, const FluidDrain2D& drain) {
                    write_fluid_region(w, drain.region);
                    w.field("enabled", drain.enabled);
                });
                write_fluid_list(out, "sensors", c.sensors, [](ComponentWriter& w, const FluidSensor2D& sensor) {
                    write_fluid_region(w, sensor.region);
                    w.field("threshold", sensor.threshold);
                    w.field("hysteresis", sensor.hysteresis);
                });
                out.field("save_particles", c.save_particles);
                if (c.save_particles) {
                    out.field("state", base64_encode(c.save_state()));
                }
            },
            [](ComponentReader& in, Fluid2D& c) {
                (void)in.field("radius", c.params.radius);
                (void)in.field("smoothing_length", c.params.smoothing_length);
                (void)in.field("rest_density", c.params.rest_density);
                (void)in.field("viscosity", c.params.viscosity);
                (void)in.field("relaxation", c.params.relaxation);
                (void)in.field("tensile_strength", c.params.tensile_strength);
                (void)in.field("iterations", c.params.iterations);
                (void)in.field("substeps", c.params.substeps);
                (void)in.field("max_particles", c.params.max_particles);
                (void)in.field("gravity_scale", c.params.gravity_scale);
                (void)in.field("friction", c.params.friction);
                (void)in.field("restitution", c.params.restitution);
                (void)in.field("density", c.params.density);
                (void)in.field("cohesion", c.params.cohesion);
                (void)in.field("vorticity", c.params.vorticity);
                (void)in.field("layer", c.params.layer);
                (void)in.field("mask", c.params.mask);
                (void)in.field("sleep", c.params.sleep);
                (void)in.field("sleep_speed", c.params.sleep_speed);
                (void)in.field("sleep_ticks", c.params.sleep_ticks);
                (void)in.field("wake_speed", c.params.wake_speed);
                read_fluid_list(in, "sources", c.sources, [](ComponentReader& r, FluidSource2D& source) {
                    read_fluid_region(r, source.region);
                    (void)r.field("rate", source.rate);
                    (void)r.field("velocity", source.velocity);
                    (void)r.field("enabled", source.enabled);
                });
                read_fluid_list(in, "drains", c.drains, [](ComponentReader& r, FluidDrain2D& drain) {
                    read_fluid_region(r, drain.region);
                    (void)r.field("enabled", drain.enabled);
                });
                read_fluid_list(in, "sensors", c.sensors, [](ComponentReader& r, FluidSensor2D& sensor) {
                    read_fluid_region(r, sensor.region);
                    (void)r.field("threshold", sensor.threshold);
                    (void)r.field("hysteresis", sensor.hysteresis);
                });
                (void)in.field("save_particles", c.save_particles);
                std::string state;
                if (in.field("state", state) && !state.empty()) {
                    const std::optional<std::vector<std::uint8_t>> bytes = base64_decode(state);
                    if (!bytes || !c.load_state(*bytes)) {
                        in.fail("fluid_2d state is not a fluid state (Fluid2D::save_state as base64)");
                    }
                }
            });

    serializer.register_component<FluidBox2D>(
            "fluid_box_2d",
            [](ComponentWriter& out, const FluidBox2D& c) {
                out.field("size", c.size);
                out.field("velocity", c.velocity);
                out.field("filled", c.filled);
            },
            [](ComponentReader& in, FluidBox2D& c) {
                (void)in.field("size", c.size);
                (void)in.field("velocity", c.velocity);
                (void)in.field("filled", c.filled);
            });

    serializer.register_component<CircleCollider2D>(
            "circle_collider_2d",
            [](ComponentWriter& out, const CircleCollider2D& c) {
                out.field("radius", c.radius);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, CircleCollider2D& c) {
                (void)in.field("radius", c.radius);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<RigidBody3D>(
            "rigid_body_3d",
            [](ComponentWriter& out, const RigidBody3D& c) {
                out.field("velocity", c.velocity);
                out.field("angular_velocity", c.angular_velocity);
                out.field("type", body_type_name(c.type));
                out.field("mass", c.mass);
                out.field("gravity_scale", c.gravity_scale);
                out.field("sleeping", c.sleeping);
            },
            [](ComponentReader& in, RigidBody3D& c) {
                (void)in.field("velocity", c.velocity);
                (void)in.field("angular_velocity", c.angular_velocity);
                read_body_type(in, c.type, "rigid_body_3d");
                (void)in.field("mass", c.mass);
                (void)in.field("gravity_scale", c.gravity_scale);
                (void)in.field("sleeping", c.sleeping);
            });

    serializer.register_component<BoxCollider3D>(
            "box_collider_3d",
            [](ComponentWriter& out, const BoxCollider3D& c) {
                out.field("size", c.size);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, BoxCollider3D& c) {
                (void)in.field("size", c.size);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<SphereCollider3D>(
            "sphere_collider_3d",
            [](ComponentWriter& out, const SphereCollider3D& c) {
                out.field("radius", c.radius);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, SphereCollider3D& c) {
                (void)in.field("radius", c.radius);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<CapsuleCollider3D>(
            "capsule_collider_3d",
            [](ComponentWriter& out, const CapsuleCollider3D& c) {
                out.field("radius", c.radius);
                out.field("height", c.height);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
                out.field("friction", c.friction);
                out.field("restitution", c.restitution);
            },
            [](ComponentReader& in, CapsuleCollider3D& c) {
                (void)in.field("radius", c.radius);
                (void)in.field("height", c.height);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
                (void)in.field("friction", c.friction);
                (void)in.field("restitution", c.restitution);
            });

    serializer.register_component<PointerTarget>(
            "pointer_target",
            [](ComponentWriter& out, const PointerTarget& c) {
                out.field("size", c.size);
                out.field("pivot", c.pivot);
                out.field("layer", c.layer);
                out.field("order_in_layer", c.order_in_layer);
                out.field("enabled", c.enabled);
            },
            [](ComponentReader& in, PointerTarget& c) {
                (void)in.field("size", c.size);
                (void)in.field("pivot", c.pivot);
                (void)in.field("layer", c.layer);
                (void)in.field("order_in_layer", c.order_in_layer);
                (void)in.field("enabled", c.enabled);
            });

    serializer.register_component<PrototypeInstance>(
            "prototype_instance",
            [](ComponentWriter& out, const PrototypeInstance& c) {
                out.field("prototype", c.prototype);
                out.field("entity_sub_id", c.entity_sub_id);
            },
            [](ComponentReader& in, PrototypeInstance& c) {
                (void)in.field("prototype", c.prototype);
                (void)in.field("entity_sub_id", c.entity_sub_id);
            });

    serializer.register_component<ModelPlacement>(
            "model",
            [](ComponentWriter& out, const ModelPlacement& placement) {
                out.field("model", placement.model);
                out.field("clip", placement.clip);
            },
            [](ComponentReader& in, ModelPlacement& placement) {
                (void)in.field("model", placement.model);
                (void)in.field("clip", placement.clip);
            });
}

} // namespace

WorldSerializer::WorldSerializer() {
    register_builtin_components(*this, entries_);
    set_field_filter("sprite", "texture", AssetFilter{.importer = ImporterKind::Texture});
    set_field_filter("sprite", "material", AssetFilter{.importer = ImporterKind::Material});
    set_field_filter("sprite", "mesh", AssetFilter{.importer = ImporterKind::Mesh});
    set_field_filter("renderable", "material", AssetFilter{.importer = ImporterKind::Material});
    set_field_filter("renderable", "mesh", AssetFilter{.importer = ImporterKind::Mesh});
    set_field_filter("tilemap", "texture", AssetFilter{.importer = ImporterKind::Texture});
    set_field_filter("model", "model", AssetFilter{.importer = ImporterKind::Model});
}

void WorldSerializer::set_field_filter(std::string_view component, std::string_view field, AssetFilter filter) {
    field_filters_.insert_or_assign(std::make_pair(std::string(component), std::string(field)), std::move(filter));
}

std::optional<AssetFilter> WorldSerializer::field_filter(std::string_view component, std::string_view field) const {
    const auto it = field_filters_.find(std::make_pair(std::string(component), std::string(field)));
    return it == field_filters_.end() ? std::nullopt : std::optional<AssetFilter>(it->second);
}

const WorldSerializer::Entry* WorldSerializer::find(std::string_view name) const {
    const auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second;
}

std::expected<InstantiatedWorld, WorldLoadError> instantiate_world(
        World& world, const WorldAsset& asset, AssetsDb* assets, const WorldSerializer* serializer,
        UnknownComponentMode unknown_mode) {
    const WorldSerializer& active_serializer =
            serializer != nullptr ? *serializer : world.ctx<WorldSerializer>();

    std::vector<EntityRecord> expanded_entities;
    expanded_entities.reserve(asset.entities.size());

    std::unordered_map<std::string, const EntityRecord*> explicit_entity_overrides;
    explicit_entity_overrides.reserve(asset.entities.size());
    for (const EntityRecord& record : asset.entities) {
        explicit_entity_overrides.emplace(record.id, &record);
    }

    std::unordered_set<std::string> handled_child_overrides;

    for (const EntityRecord& record : asset.entities) {
        if (!record.prototype.has_value()) {
            if (handled_child_overrides.contains(record.id)) {
                continue;
            }
            expanded_entities.push_back(record);
            continue;
        }

        if (assets == nullptr) {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::AssetError,
                    std::format("entity \"{}\" specifies prototype \"{}\" but no AssetsDb provided",
                                record.id, record.prototype->hex())});
        }

        auto proto = assets->try_get<PrototypeAsset>(*record.prototype);
        if (!proto) {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::AssetError,
                    std::format("entity \"{}\": failed to load prototype \"{}\"",
                                record.id, record.prototype->hex())});
        }

        auto resolved_proto = resolve_prototype_asset(**proto, assets);
        if (!resolved_proto) {
            return std::unexpected(WorldLoadError{
                    resolved_proto.error().kind == PrototypeErrorKind::AssetError
                            ? WorldErrorKind::AssetError
                            : WorldErrorKind::InvalidEntity,
                    std::format("entity \"{}\": {}", record.id, resolved_proto.error().message)});
        }

        if (resolved_proto->entities.empty()) {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::InvalidEntity,
                    std::format("entity \"{}\": prototype \"{}\" has no entities",
                                record.id, record.prototype->hex())});
        }

        const std::string& root_proto_id = resolved_proto->entities[0].id;
        for (const EntityRecord& p_ent : resolved_proto->entities) {
            EntityRecord spawned;
            const bool is_root = (&p_ent == &resolved_proto->entities[0]);
            if (is_root) {
                spawned.id = record.id;
                spawned.parent = record.parent;
                spawned.components = p_ent.components;
                for (const auto& [comp_name, comp_table] : record.components) {
                    auto comp_it = spawned.components.find(comp_name);
                    if (comp_it != spawned.components.end()) {
                        merge_field_tables(comp_it->second, comp_table);
                    } else {
                        spawned.components.insert_or_assign(comp_name, comp_table);
                    }
                }
            } else {
                spawned.id = record.id + "/" + p_ent.id;
                if (p_ent.parent == root_proto_id || p_ent.parent.empty()) {
                    spawned.parent = record.id;
                } else {
                    spawned.parent = record.id + "/" + p_ent.parent;
                }
                spawned.components = p_ent.components;

                auto override_it = explicit_entity_overrides.find(spawned.id);
                if (override_it != explicit_entity_overrides.end()) {
                    const EntityRecord* child_override = override_it->second;
                    for (const auto& [comp_name, comp_table] : child_override->components) {
                        auto comp_it = spawned.components.find(comp_name);
                        if (comp_it != spawned.components.end()) {
                            merge_field_tables(comp_it->second, comp_table);
                        } else {
                            spawned.components.insert_or_assign(comp_name, comp_table);
                        }
                    }
                    handled_child_overrides.insert(spawned.id);
                }
            }

            FieldTable proto_inst_table;
            proto_inst_table["prototype"] = FieldValue{record.prototype->uri()};
            proto_inst_table["entity_sub_id"] = FieldValue{p_ent.id};
            spawned.components.insert_or_assign("prototype_instance", std::move(proto_inst_table));

            expanded_entities.push_back(std::move(spawned));
        }
    }

    if (unknown_mode == UnknownComponentMode::Reject) {
        for (const EntityRecord& record : expanded_entities) {
            for (const auto& [comp_name, _] : record.components) {
                if (active_serializer.find(comp_name) == nullptr) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::UnknownComponent,
                            std::format("entity \"{}\": unknown component \"{}\"", record.id, comp_name)});
                }
            }
        }
    }

    InstantiatedWorld result;
    result.entities.reserve(expanded_entities.size());
    result.by_id.reserve(expanded_entities.size());
    for (const EntityRecord& record : expanded_entities) {
        const Entity entity = world.create();
        world.emplace<EntityName>(entity, EntityName{record.id});
        result.entities.push_back(entity);
        result.by_id.emplace(record.id, entity);
    }

    const auto lookup_entity = [&result](std::string_view id) -> std::optional<Entity> {
        const auto it = result.by_id.find(std::string(id));
        if (it == result.by_id.end()) {
            return std::nullopt;
        }
        return it->second;
    };

    const auto rollback = [&world, &result]() {
        for (const Entity e : result.entities) {
            if (world.valid(e)) {
                world.destroy(e);
            }
        }
    };

    for (std::size_t i = 0; i < expanded_entities.size(); ++i) {
        const EntityRecord& record = expanded_entities[i];
        const Entity entity = result.entities[i];
        if (!record.parent.empty()) {
            const auto parent_it = result.by_id.find(record.parent);
            if (parent_it == result.by_id.end()) {
                rollback();
                return std::unexpected(WorldLoadError{
                        WorldErrorKind::UnknownParent,
                        std::format("entity \"{}\" references unknown parent \"{}\"", record.id, record.parent)});
            }
            if (!set_parent(world, entity, parent_it->second)) {
                rollback();
                return std::unexpected(WorldLoadError{
                        WorldErrorKind::HierarchyCycle,
                        std::format("failed to set parent \"{}\" on entity \"{}\"", record.parent, record.id)});
            }
        }
    }

    for (std::size_t i = 0; i < expanded_entities.size(); ++i) {
        const EntityRecord& record = expanded_entities[i];
        const Entity entity = result.entities[i];
        for (const auto& [comp_name, comp_fields] : record.components) {
            const WorldSerializer::Entry* entry = active_serializer.find(comp_name);
            if (entry == nullptr) {
                UnknownComponents* unknown = world.try_get<UnknownComponents>(entity);
                if (unknown == nullptr) {
                    unknown = &world.emplace<UnknownComponents>(entity);
                }
                unknown->components.insert_or_assign(comp_name, comp_fields);
                continue;
            }
            ComponentReader reader(comp_fields, lookup_entity, assets);
            if (!entry->read(world, entity, reader)) {
                const bool is_asset_err = reader.error().find("asset error") != std::string_view::npos;
                WorldLoadError err{
                        is_asset_err ? WorldErrorKind::AssetError : WorldErrorKind::InvalidField,
                        std::format(
                                "entity \"{}\", component \"{}\": {}", record.id, comp_name, reader.error())};
                rollback();
                return std::unexpected(std::move(err));
            }
        }
    }

    if (!asset.active_camera.empty()) {
        const auto cam_it = result.by_id.find(asset.active_camera);
        if (cam_it == result.by_id.end()) {
            rollback();
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::UnknownActiveCamera,
                    std::format("active_camera \"{}\" does not match any entity", asset.active_camera)});
        }
        world.ctx<ActiveCamera>().entity = cam_it->second;
    }

    if (assets != nullptr) {
        for (const Entity entity : result.entities) {
            if (world.try_get<ModelPlacement>(entity) == nullptr) {
                continue;
            }
            auto expanded = expand_model(world, entity, *assets);
            if (!expanded) {
                rollback();
                return std::unexpected(std::move(expanded.error()));
            }
        }
    }

    update_world_transforms(world);
    return result;
}

std::expected<InstantiatedWorld, WorldLoadError> load_world_from_toml(
        World& world, std::string_view toml_text, AssetsDb* assets, const WorldSerializer* serializer,
        UnknownComponentMode unknown_mode) {
    auto parsed = parse_world_asset(toml_text);
    if (!parsed) {
        return std::unexpected(std::move(parsed.error()));
    }
    return instantiate_world(world, *parsed, assets, serializer, unknown_mode);
}

WorldAsset capture_world_asset(const World& world, const WorldSerializer* serializer) {
    const WorldSerializer default_serializer;
    const WorldSerializer* ctx_serializer = world.try_ctx<WorldSerializer>();
    const WorldSerializer& active_serializer = serializer != nullptr
            ? *serializer
            : (ctx_serializer != nullptr ? *ctx_serializer : default_serializer);

    const std::vector<Entity> entities = world.alive_entities();
    std::unordered_map<std::uint32_t, std::string> id_by_index;
    std::unordered_set<std::string> used_ids;
    id_by_index.reserve(entities.size());
    used_ids.reserve(entities.size());

    for (const Entity entity : entities) {
        if (world.try_get<ModelPartTag>(entity) != nullptr) {
            continue;
        }
        std::string chosen;
        if (const EntityName* name = world.try_get<EntityName>(entity); name != nullptr && !name->id.empty()) {
            chosen = name->id;
        } else {
            chosen = std::format("entity_{}", entity.index);
        }
        if (used_ids.contains(chosen)) {
            chosen = std::format("{}_{}", chosen, entity.index);
        }
        used_ids.insert(chosen);
        id_by_index.emplace(entity.index, std::move(chosen));
    }

    const auto entity_to_id = [&world, &id_by_index](Entity target) -> std::string {
        if (!world.valid(target)) {
            return {};
        }
        const auto it = id_by_index.find(target.index);
        return it == id_by_index.end() ? std::string{} : it->second;
    };

    WorldAsset asset;
    asset.version = 1;
    if (const ActiveCamera* active = world.try_ctx<ActiveCamera>();
            active != nullptr && world.valid(active->entity)) {
        asset.active_camera = entity_to_id(active->entity);
    }

    asset.entities.reserve(entities.size());
    for (const Entity entity : entities) {
        if (world.try_get<ModelPartTag>(entity) != nullptr) {
            continue;
        }
        EntityRecord record;
        record.id = entity_to_id(entity);
        if (const Parent* parent = world.try_get<Parent>(entity);
                parent != nullptr && world.valid(parent->entity)) {
            record.parent = entity_to_id(parent->entity);
        }
        for (const auto& [comp_name, entry] : active_serializer.entries()) {
            FieldTable table;
            ComponentWriter writer(table, entity_to_id);
            if (entry.write(world, entity, writer)) {
                record.components.emplace(comp_name, std::move(table));
            }
        }
        if (const UnknownComponents* unknown = world.try_get<UnknownComponents>(entity)) {
            for (const auto& [comp_name, table] : unknown->components) {
                if (!record.components.contains(comp_name)) {
                    record.components.emplace(comp_name, table);
                }
            }
        }
        asset.entities.push_back(std::move(record));
    }

    return asset;
}

std::string save_world_to_toml(const World& world, const WorldSerializer* serializer) {
    return serialize_world_asset(capture_world_asset(world, serializer));
}

} // namespace engine::ecs
