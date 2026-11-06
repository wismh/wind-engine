#include <engine/render/lights.h>

#include <engine/ecs/hierarchy.h>
#include <engine/log.h>
#include <engine/render/aabb.h>
#include <engine/render/ambient_light.h>
#include <engine/render/directional_light.h>
#include <engine/render/point_light.h>
#include <engine/render/spot_light.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <vector>

namespace engine::render {
namespace {

bool warned_too_many_directional = false;
bool warned_too_many_local = false;

glm::vec3 light_direction(ecs::World& world, ecs::Entity entity) {
    const glm::vec3 forward = -glm::vec3(ecs::world_matrix(world, entity)[2]);
    const float length = glm::length(forward);
    if (!(length > 1e-6f)) {
        return {0.0f, 0.0f, -1.0f};
    }
    return forward / length;
}

glm::vec3 light_position(ecs::World& world, ecs::Entity entity) {
    return glm::vec3(ecs::world_matrix(world, entity)[3]);
}

// `saturate(dot(-L, forward) * scale + bias)` is 1 at the inner cosine and 0 at the outer cosine. An inner angle
// that is not smaller than the outer angle is a hard edge at the outer cosine.
void pack_cone(float inner_angle, float outer_angle, float& scale, float& bias) {
    const float outer_cos = std::cos(outer_angle);
    if (!(inner_angle < outer_angle)) {
        scale = 1.0e4f;
        bias = -outer_cos * scale;
        return;
    }
    const float inner_cos = std::cos(inner_angle);
    const float span = inner_cos - outer_cos;
    if (!(span > 1e-6f)) {
        scale = 1.0e4f;
        bias = -outer_cos * scale;
        return;
    }
    scale = 1.0f / span;
    bias = -outer_cos * scale;
}

struct GatheredLocal {
    ecs::Entity entity{};
    SceneLight light{};
    float distance = 0.0f;
};

bool sphere_in_view(std::span<const Frustum> frustums, const glm::vec3& position, float range) {
    const glm::vec3 extent{std::max(range, 0.0f)};
    const Aabb box{.min = position - extent, .max = position + extent};
    return visible_in_any(frustums, box);
}

SceneLights select_lights(ecs::World& world, std::span<const Frustum> frustums, const glm::vec3& camera_position) {
    std::vector<ecs::Entity> directional;
    for (const ecs::Entity entity : world.view<DirectionalLight>()) {
        directional.push_back(entity);
    }

    bool any = !directional.empty();
    glm::vec3 ambient{0.0f};
    auto ambients = world.view<AmbientLight>();
    for (const ecs::Entity entity : ambients) {
        const AmbientLight& light = ambients.get<AmbientLight>(entity);
        ambient += light.color * light.intensity;
        any = true;
    }

    std::vector<GatheredLocal> local;
    auto points = world.view<PointLight>();
    for (const ecs::Entity entity : points) {
        const PointLight& source = points.get<PointLight>(entity);
        any = true;
        const glm::vec3 position = light_position(world, entity);
        if (!sphere_in_view(frustums, position, source.range)) {
            continue;
        }
        SceneLight light;
        light.kind = LightKind::Point;
        light.color = source.color * source.intensity;
        light.position = position;
        light.range = source.range;
        local.push_back(GatheredLocal{entity, light, glm::length(position - camera_position)});
    }
    auto spots = world.view<SpotLight>();
    for (const ecs::Entity entity : spots) {
        const SpotLight& source = spots.get<SpotLight>(entity);
        any = true;
        const glm::vec3 position = light_position(world, entity);
        if (!sphere_in_view(frustums, position, source.range)) {
            continue;
        }
        SceneLight light;
        light.kind = LightKind::Spot;
        light.color = source.color * source.intensity;
        light.direction = light_direction(world, entity);
        light.position = position;
        light.range = source.range;
        pack_cone(source.inner_angle, source.outer_angle, light.spot_scale, light.spot_bias);
        local.push_back(GatheredLocal{entity, light, glm::length(position - camera_position)});
    }

    if (!any) {
        return default_scene_lights();
    }

    std::sort(directional.begin(), directional.end());
    if (directional.size() > kMaxDirectionalLights && !warned_too_many_directional) {
        warned_too_many_directional = true;
        log::warn(std::format(
                "Render: {} directional lights, only the first {} by entity index are used (warned once)",
                directional.size(), kMaxDirectionalLights));
    }

    std::sort(local.begin(), local.end(), [](const GatheredLocal& a, const GatheredLocal& b) {
        if (a.distance < b.distance) {
            return true;
        }
        if (b.distance < a.distance) {
            return false;
        }
        return a.entity < b.entity;
    });

    SceneLights lights;
    lights.ambient = ambient;
    const std::size_t directional_count = std::min(directional.size(), kMaxDirectionalLights);
    for (std::size_t i = 0; i < directional_count; ++i) {
        const DirectionalLight& source = *world.try_get<DirectionalLight>(directional[i]);
        SceneLight& light = lights.lights[i];
        light.kind = LightKind::Directional;
        light.color = source.color * source.intensity;
        light.direction = light_direction(world, directional[i]);
    }

    const std::size_t room = kMaxForwardLights - directional_count;
    if (local.size() > room && !warned_too_many_local) {
        warned_too_many_local = true;
        log::warn(std::format(
                "Render: {} point and spot lights in view, only the nearest {} fit with the directional lights "
                "(warned once)",
                local.size(), room));
    }
    const std::size_t local_count = std::min(local.size(), room);
    for (std::size_t i = 0; i < local_count; ++i) {
        lights.lights[directional_count + i] = local[i].light;
    }
    lights.count = static_cast<int>(directional_count + local_count);
    return lights;
}

}

SceneLights default_scene_lights() {
    SceneLights lights;
    SceneLight& sun = lights.lights[0];
    sun.kind = LightKind::Directional;
    sun.color = kDefaultLightColor;
    sun.direction = glm::normalize(kDefaultLightDirection);
    lights.count = 1;
    lights.ambient = kDefaultAmbient;
    return lights;
}

SceneLights collect_scene_lights(ecs::World& world) {
    return select_lights(world, {}, glm::vec3{0.0f});
}

SceneLights collect_scene_lights(ecs::World& world, std::span<const Frustum> frustums, glm::vec3 camera_position) {
    return select_lights(world, frustums, camera_position);
}

}
