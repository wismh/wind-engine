#include <engine/ecs/physics.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/transform.h>

#include <set>
#include <utility>
#include <vector>

namespace engine {
namespace {

struct PhysicsOverlapState {
    std::set<std::pair<ecs::Entity, ecs::Entity>> last{};
};

struct ColliderItem {
    ecs::Entity entity{};
    glm::vec3 position{0};
    glm::vec3 size{1.f, 1.f, 1.f};
};

[[nodiscard]] std::pair<ecs::Entity, ecs::Entity> make_pair(ecs::Entity a, ecs::Entity b) {
    if (b < a) {
        return {b, a};
    }
    return {a, b};
}

[[nodiscard]] bool aabb_overlap_xy(const ColliderItem& a, const ColliderItem& b) {
    const glm::vec3 a_min = a.position - a.size * 0.5f;
    const glm::vec3 a_max = a.position + a.size * 0.5f;
    const glm::vec3 b_min = b.position - b.size * 0.5f;
    const glm::vec3 b_max = b.position + b.size * 0.5f;
    return (a_min.x <= b_max.x && a_max.x >= b_min.x) && (a_min.y <= b_max.y && a_max.y >= b_min.y);
}

}

void run_physics(ecs::World& world) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    world.view<Transform, RigidBody>().each([dt](Transform& transform, const RigidBody& body) {
        transform.position += body.velocity * dt;
    });

    std::vector<ColliderItem> colliders;
    world.view<Transform, BoxCollider>().each(
            [&](ecs::Entity entity, const Transform& transform, const BoxCollider& collider) {
                colliders.push_back(ColliderItem{entity, transform.position, collider.size});
            });

    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    std::set<std::pair<ecs::Entity, ecs::Entity>> current;
    ecs::EventWriter<CollisionEvent> writer{world};

    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            if (!aabb_overlap_xy(colliders[i], colliders[j])) {
                continue;
            }
            const auto pair = make_pair(colliders[i].entity, colliders[j].entity);
            current.insert(pair);
            if (!state.last.contains(pair)) {
                writer.send(CollisionEvent{pair.first, pair.second});
            }
        }
    }

    state.last = std::move(current);
}

}
