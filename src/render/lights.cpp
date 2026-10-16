#include <engine/render/lights.h>

#include <engine/ecs/hierarchy.h>
#include <engine/log.h>
#include <engine/render/ambient_light.h>
#include <engine/render/directional_light.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <vector>

namespace engine::render {
namespace {

bool warned_too_many_lights = false;

glm::vec3 light_direction(ecs::World& world, ecs::Entity entity) {
    const glm::vec3 forward = -glm::vec3(ecs::world_matrix(world, entity)[2]);
    const float length = glm::length(forward);
    if (!(length > 1e-6f)) {
        return {0.0f, 0.0f, -1.0f};
    }
    return forward / length;
}

}

SceneLights default_scene_lights() {
    SceneLights lights;
    lights.directions[0] = glm::normalize(kDefaultLightDirection);
    lights.colors[0] = kDefaultLightColor;
    lights.directional_count = 1;
    lights.ambient = kDefaultAmbient;
    return lights;
}

SceneLights collect_scene_lights(ecs::World& world) {
    std::vector<ecs::Entity> directional;
    for (const ecs::Entity entity : world.view<DirectionalLight>()) {
        directional.push_back(entity);
    }
    bool any_ambient = false;
    glm::vec3 ambient{0.0f};
    auto ambients = world.view<AmbientLight>();
    for (const ecs::Entity entity : ambients) {
        const AmbientLight& light = ambients.get<AmbientLight>(entity);
        ambient += light.color * light.intensity;
        any_ambient = true;
    }
    if (directional.empty() && !any_ambient) {
        return default_scene_lights();
    }

    std::sort(directional.begin(), directional.end());
    if (directional.size() > kMaxDirectionalLights && !warned_too_many_lights) {
        warned_too_many_lights = true;
        log::warn(std::format("Render: {} directional lights, only the first {} by entity index are used (warned once)",
                directional.size(), kMaxDirectionalLights));
    }

    SceneLights lights;
    lights.ambient = ambient;
    const std::size_t count = std::min(directional.size(), kMaxDirectionalLights);
    for (std::size_t i = 0; i < count; ++i) {
        const DirectionalLight& light = *world.try_get<DirectionalLight>(directional[i]);
        lights.directions[i] = light_direction(world, directional[i]);
        lights.colors[i] = light.color * light.intensity;
    }
    lights.directional_count = static_cast<int>(count);
    return lights;
}

}
