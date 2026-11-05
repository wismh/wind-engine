#include <engine/ecs/model.h>

#include <engine/builtin_ids.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/resources/assets_db.h>

#include <format>
#include <limits>
#include <unordered_map>

namespace engine::ecs {
namespace {

render::RenderState material_state(const render::ModelMaterial& material) {
    render::RenderState state;
    state.depth_test = true;
    state.cull = material.double_sided ? render::CullMode::None : render::CullMode::Back;
    switch (material.alpha) {
        case render::ModelAlpha::Mask:
            state.blend = render::BlendMode::Opaque;
            state.depth_write = true;
            state.alpha_cutoff = material.alpha_cutoff;
            break;
        case render::ModelAlpha::Blend:
            state.blend = render::BlendMode::Alpha;
            state.depth_write = false;
            break;
        case render::ModelAlpha::Opaque:
            state.blend = render::BlendMode::Opaque;
            state.depth_write = true;
            break;
    }
    return state;
}

std::expected<std::shared_ptr<render::IShader>, WorldLoadError> load_shader(
        AssetsDb& assets, AssetId id, const char* name) {
    auto shader = assets.try_get<render::IShader>(id);
    if (!shader) {
        return std::unexpected(WorldLoadError{
                WorldErrorKind::AssetError,
                std::format("model shader {} ({})", name, to_string(shader.error()))});
    }
    return std::move(*shader);
}

}

void apply_model_poses(World& world, const render::ModelAnimator& animator) {
    for (const render::ModelPart& part : animator.parts) {
        if (!part.drives_transform || !world.valid(part.entity)) {
            continue;
        }
        if (part.node < 0 || static_cast<std::size_t>(part.node) >= animator.node_global.size()) {
            continue;
        }
        const std::size_t node = static_cast<std::size_t>(part.node);
        const engine::Transform pose = render::transform_from_matrix(animator.node_global[node]);
        if (engine::Transform* transform = world.try_get<engine::Transform>(part.entity)) {
            *transform = pose;
        } else {
            world.emplace<engine::Transform>(part.entity, pose);
        }
    }
}

struct ModelShaders {
    std::shared_ptr<render::IShader> lit;
    std::shared_ptr<render::IShader> unlit;
    std::shared_ptr<render::IShader> skin_lit;
    std::shared_ptr<render::IShader> skin_unlit;
};

std::expected<ModelShaders, WorldLoadError> load_model_shaders(AssetsDb& assets) {
    ModelShaders shaders;
    auto lit = load_shader(assets, builtin::shader_lit, "lit");
    if (!lit) {
        return std::unexpected(std::move(lit.error()));
    }
    auto unlit = load_shader(assets, builtin::shader_unlit, "unlit");
    if (!unlit) {
        return std::unexpected(std::move(unlit.error()));
    }
    auto skin_lit = load_shader(assets, builtin::shader_skin_lit, "skin_lit");
    if (!skin_lit) {
        return std::unexpected(std::move(skin_lit.error()));
    }
    auto skin_unlit = load_shader(assets, builtin::shader_skin_unlit, "skin_unlit");
    if (!skin_unlit) {
        return std::unexpected(std::move(skin_unlit.error()));
    }
    shaders.lit = std::move(*lit);
    shaders.unlit = std::move(*unlit);
    shaders.skin_lit = std::move(*skin_lit);
    shaders.skin_unlit = std::move(*skin_unlit);
    return shaders;
}

// Builds the part hierarchy on `root`, which already exists. On failure the parts created so far are destroyed with
// `root` only when `destroy_root` is set; a world-file root is left for the caller's rollback.
std::expected<void, WorldLoadError> build_model(
        World& world,
        Entity root,
        std::shared_ptr<render::ModelAsset> model,
        render::IGraphicFactory& factory,
        const ModelShaders& shaders,
        std::string clip,
        bool destroy_root) {
    if (!world.try_get<engine::Transform>(root)) {
        world.emplace<engine::Transform>(root, engine::Transform{});
    }

    render::ModelAnimator animator;
    animator.model = std::move(model);
    render::ModelLayer& base = animator.layers.emplace_back();
    base.clip = std::move(clip);
    if (base.clip.empty() && !animator.model->clips.empty()) {
        base.clip = animator.model->clips.front().name;
    }

    std::unordered_map<int, Entity> driver_for_node;
    std::vector<std::shared_ptr<render::ITexture>> textures(animator.model->textures.size());
    for (std::size_t i = 0; i < animator.model->textures.size(); ++i) {
        const render::ModelTexture& source = animator.model->textures[i];
        render::TextureDesc desc;
        desc.width = source.width;
        desc.height = source.height;
        desc.rgba = source.rgba;
        desc.wrap = engine::WrapMode::Repeat;
        textures[i] = factory.create_texture(desc);
        if (!textures[i]) {
            if (destroy_root) {
                world.destroy(root);
            }
            return std::unexpected(WorldLoadError{WorldErrorKind::AssetError, "model texture failed to upload"});
        }
    }

    const render::ModelAsset& asset = *animator.model;
    for (std::size_t index = 0; index < asset.primitives.size(); ++index) {
        const render::ModelPrimitive& primitive = asset.primitives[index];
        std::shared_ptr<render::IMesh> mesh = factory.create_mesh(primitive.mesh);
        if (!mesh) {
            if (destroy_root) {
                world.destroy(root);
            }
            return std::unexpected(WorldLoadError{WorldErrorKind::AssetError, "model mesh failed to upload"});
        }
        const render::ModelMaterial* material = nullptr;
        if (primitive.material >= 0 && static_cast<std::size_t>(primitive.material) < asset.materials.size()) {
            material = &asset.materials[static_cast<std::size_t>(primitive.material)];
        }
        const bool unlit_material = material != nullptr && material->unlit;
        const bool skinned = primitive.skin >= 0;
        std::shared_ptr<render::IShader> shader = unlit_material ? (skinned ? shaders.skin_unlit : shaders.unlit)
                                                                : (skinned ? shaders.skin_lit : shaders.lit);
        std::shared_ptr<render::ITexture> albedo;
        glm::vec4 color{1.0f};
        render::RenderState state;
        state.blend = render::BlendMode::Opaque;
        state.depth_test = true;
        state.depth_write = true;
        state.cull = render::CullMode::Back;
        if (material != nullptr) {
            color = material->base_color;
            state = material_state(*material);
            if (material->texture >= 0 && static_cast<std::size_t>(material->texture) < textures.size()) {
                albedo = textures[static_cast<std::size_t>(material->texture)];
            }
        }
        auto gpu_material = std::make_shared<render::Material>(std::move(shader), std::move(albedo), color, state);

        const Entity entity = world.create();
        world.emplace<ModelPartTag>(entity);
        render::ModelPart part;
        part.entity = entity;
        part.node = primitive.node;
        const auto driver = driver_for_node.find(primitive.node);
        if (driver == driver_for_node.end()) {
            part.drives_transform = true;
            driver_for_node.emplace(primitive.node, entity);
            if (!set_parent(world, entity, root)) {
                world.destroy(entity);
                if (destroy_root) {
                    world.destroy(root);
                }
                return std::unexpected(
                        WorldLoadError{WorldErrorKind::HierarchyCycle, "model part could not be parented"});
            }
        } else if (!set_parent(world, entity, driver->second)) {
            world.destroy(entity);
            if (destroy_root) {
                world.destroy(root);
            }
            return std::unexpected(
                    WorldLoadError{WorldErrorKind::HierarchyCycle, "model part could not be parented"});
        }
        world.emplace<render::Renderable>(entity, render::Renderable{std::move(mesh), std::move(gpu_material)});
        if (skinned) {
            world.emplace<render::SkinDraw>(entity, render::SkinDraw{
                    .animator = root, .skin = primitive.skin, .primitive = static_cast<int>(index)});
        }
        animator.parts.push_back(part);
    }

    render::tick_model_animator(animator, 0.0f);
    apply_model_poses(world, animator);
    world.emplace<render::ModelAnimator>(root, std::move(animator));
    return {};
}

std::expected<Entity, WorldLoadError> spawn_model(
        World& world, AssetId id, AssetsDb& assets, const SpawnModelParams& params) {
    auto model = assets.try_get<render::ModelAsset>(id);
    if (!model) {
        return std::unexpected(WorldLoadError{
                WorldErrorKind::AssetError, std::format("model asset ({})", to_string(model.error()))});
    }
    render::IGraphicFactory* factory = assets.graphic_factory();
    if (factory == nullptr) {
        return std::unexpected(WorldLoadError{WorldErrorKind::AssetError, "model spawn has no graphic factory"});
    }
    auto shaders = load_model_shaders(assets);
    if (!shaders) {
        return std::unexpected(std::move(shaders.error()));
    }

    const Entity root = world.create();
    if (!params.name.empty()) {
        world.emplace<EntityName>(root, EntityName{params.name});
    }
    engine::Transform root_transform;
    if (params.position) {
        root_transform.position = *params.position;
    }
    if (params.rotation) {
        root_transform.rotation = *params.rotation;
    }
    if (params.scale) {
        root_transform.scale = *params.scale;
    }
    world.emplace<engine::Transform>(root, root_transform);
    if (world.valid(params.parent) && !set_parent(world, root, params.parent)) {
        world.destroy(root);
        return std::unexpected(WorldLoadError{WorldErrorKind::HierarchyCycle, "model root could not be parented"});
    }

    auto built = build_model(world, root, std::move(*model), *factory, *shaders, params.clip, true);
    if (!built) {
        return std::unexpected(std::move(built.error()));
    }
    return root;
}

std::expected<void, WorldLoadError> expand_model(World& world, Entity root, AssetsDb& assets) {
    const ModelPlacement* placement = world.try_get<ModelPlacement>(root);
    if (placement == nullptr) {
        return std::unexpected(WorldLoadError{WorldErrorKind::InvalidField, "model placement is missing"});
    }
    if (world.try_get<render::ModelAnimator>(root) != nullptr) {
        return {};
    }
    auto model = assets.try_get<render::ModelAsset>(placement->model);
    if (!model) {
        return std::unexpected(WorldLoadError{
                WorldErrorKind::AssetError, std::format("model asset ({})", to_string(model.error()))});
    }
    render::IGraphicFactory* factory = assets.graphic_factory();
    if (factory == nullptr) {
        return std::unexpected(WorldLoadError{WorldErrorKind::AssetError, "model spawn has no graphic factory"});
    }
    auto shaders = load_model_shaders(assets);
    if (!shaders) {
        return std::unexpected(std::move(shaders.error()));
    }
    return build_model(world, root, std::move(*model), *factory, *shaders, placement->clip, false);
}

std::optional<glm::mat4> joint_world(const World& world, Entity root, std::string_view name) {
    const render::ModelAnimator* animator = world.try_get<render::ModelAnimator>(root);
    if (animator == nullptr || !animator->model || animator->node_global.size() != animator->model->nodes.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < animator->model->nodes.size(); ++i) {
        if (animator->model->nodes[i].name == name) {
            return compute_world_matrix(world, root) * animator->node_global[i];
        }
    }
    return std::nullopt;
}

std::optional<std::string> nearest_joint(const World& world, Entity root, const glm::vec3& world_point) {
    const render::ModelAnimator* animator = world.try_get<render::ModelAnimator>(root);
    if (animator == nullptr || !animator->model || animator->node_global.size() != animator->model->nodes.size() ||
        animator->model->nodes.empty()) {
        return std::nullopt;
    }
    const glm::mat4 root_world = compute_world_matrix(world, root);
    float best = std::numeric_limits<float>::infinity();
    std::size_t chosen = 0;
    for (std::size_t i = 0; i < animator->model->nodes.size(); ++i) {
        const glm::vec3 position = glm::vec3((root_world * animator->node_global[i])[3]);
        const glm::vec3 delta = position - world_point;
        const float distance = glm::dot(delta, delta);
        if (distance < best) {
            best = distance;
            chosen = i;
        }
    }
    return animator->model->nodes[chosen].name;
}

}
