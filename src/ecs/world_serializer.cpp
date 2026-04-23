#include <engine/ecs/world_serializer.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/pointer_target.h>
#include <engine/ecs/transform.h>
#include <engine/render/ambient_light.h>
#include <engine/render/animation.h>
#include <engine/render/directional_light.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>

#include <format>
#include <optional>
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
};

[[nodiscard]] bool has_asset(const AssetId& id) noexcept {
    return id != AssetId{};
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
        entries.emplace("renderable", std::move(renderable_entry));
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

    serializer.register_component<RigidBody2D>(
            "rigid_body_2d",
            [](ComponentWriter& out, const RigidBody2D& c) { out.field("velocity", c.velocity); },
            [](ComponentReader& in, RigidBody2D& c) { (void)in.field("velocity", c.velocity); });

    serializer.register_component<BoxCollider2D>(
            "box_collider_2d",
            [](ComponentWriter& out, const BoxCollider2D& c) {
                out.field("size", c.size);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
            },
            [](ComponentReader& in, BoxCollider2D& c) {
                (void)in.field("size", c.size);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
            });

    serializer.register_component<CircleCollider2D>(
            "circle_collider_2d",
            [](ComponentWriter& out, const CircleCollider2D& c) {
                out.field("radius", c.radius);
                out.field("layer", c.layer);
                out.field("mask", c.mask);
                out.field("is_trigger", c.is_trigger);
            },
            [](ComponentReader& in, CircleCollider2D& c) {
                (void)in.field("radius", c.radius);
                (void)in.field("layer", c.layer);
                (void)in.field("mask", c.mask);
                (void)in.field("is_trigger", c.is_trigger);
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
}

} // namespace

WorldSerializer::WorldSerializer() {
    register_builtin_components(*this, entries_);
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

    if (unknown_mode == UnknownComponentMode::Reject) {
        for (const EntityRecord& record : asset.entities) {
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
    result.entities.reserve(asset.entities.size());
    for (const EntityRecord& record : asset.entities) {
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

    for (std::size_t i = 0; i < asset.entities.size(); ++i) {
        const EntityRecord& record = asset.entities[i];
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

    for (std::size_t i = 0; i < asset.entities.size(); ++i) {
        const EntityRecord& record = asset.entities[i];
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

    for (const Entity entity : entities) {
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
