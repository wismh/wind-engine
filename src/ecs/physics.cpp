#include <engine/ecs/physics.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/transform.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace engine {
namespace {

struct PhysicsOverlapState {
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> last{};  // value: was_trigger
};

enum class ColliderShape {
    Box,
    Circle,
};

struct ColliderItem {
    ecs::Entity entity{};
    glm::vec3 position{0};
    ColliderShape shape{ColliderShape::Box};
    glm::vec3 box_size{1.f, 1.f, 1.f};
    float radius{0.5f};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
};

[[nodiscard]] std::pair<ecs::Entity, ecs::Entity> make_pair(ecs::Entity a, ecs::Entity b) {
    if (b < a) {
        return {b, a};
    }
    return {a, b};
}

[[nodiscard]] bool layers_match(const ColliderItem& a, const ColliderItem& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

[[nodiscard]] bool aabb_overlap_xy(const ColliderItem& a, const ColliderItem& b) {
    const glm::vec3 a_min = a.position - a.box_size * 0.5f;
    const glm::vec3 a_max = a.position + a.box_size * 0.5f;
    const glm::vec3 b_min = b.position - b.box_size * 0.5f;
    const glm::vec3 b_max = b.position + b.box_size * 0.5f;
    return (a_min.x <= b_max.x && a_max.x >= b_min.x) && (a_min.y <= b_max.y && a_max.y >= b_min.y);
}

[[nodiscard]] bool circle_overlap_xy(const ColliderItem& a, const ColliderItem& b) {
    const float dx = a.position.x - b.position.x;
    const float dy = a.position.y - b.position.y;
    const float radius_sum = a.radius + b.radius;
    return (dx * dx + dy * dy) <= (radius_sum * radius_sum);
}

[[nodiscard]] bool aabb_circle_overlap_xy(const ColliderItem& box, const ColliderItem& circle) {
    const glm::vec3 box_min = box.position - box.box_size * 0.5f;
    const glm::vec3 box_max = box.position + box.box_size * 0.5f;
    const float closest_x = std::clamp(circle.position.x, box_min.x, box_max.x);
    const float closest_y = std::clamp(circle.position.y, box_min.y, box_max.y);
    const float dx = circle.position.x - closest_x;
    const float dy = circle.position.y - closest_y;
    return (dx * dx + dy * dy) <= (circle.radius * circle.radius);
}

[[nodiscard]] bool shapes_overlap(const ColliderItem& a, const ColliderItem& b) {
    if (a.shape == ColliderShape::Box && b.shape == ColliderShape::Box) {
        return aabb_overlap_xy(a, b);
    }
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
        return circle_overlap_xy(a, b);
    }
    return a.shape == ColliderShape::Box ? aabb_circle_overlap_xy(a, b) : aabb_circle_overlap_xy(b, a);
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
                colliders.push_back(ColliderItem{
                        .entity = entity,
                        .position = transform.position,
                        .shape = ColliderShape::Box,
                        .box_size = collider.size,
                        .layer = collider.layer,
                        .mask = collider.mask,
                        .is_trigger = collider.is_trigger,
                });
            });
    world.view<Transform, CircleCollider>().each(
            [&](ecs::Entity entity, const Transform& transform, const CircleCollider& collider) {
                colliders.push_back(ColliderItem{
                        .entity = entity,
                        .position = transform.position,
                        .shape = ColliderShape::Circle,
                        .radius = collider.radius,
                        .layer = collider.layer,
                        .mask = collider.mask,
                        .is_trigger = collider.is_trigger,
                });
            });

    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> current;
    ecs::EventWriter<CollisionEvent> writer{world};

    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const ColliderItem& a = colliders[i];
            const ColliderItem& b = colliders[j];
            if (!layers_match(a, b) || !shapes_overlap(a, b)) {
                continue;
            }
            const auto pair = make_pair(a.entity, b.entity);
            const bool is_trigger = a.is_trigger || b.is_trigger;
            current.emplace(pair, is_trigger);
            const CollisionPhase phase = state.last.contains(pair) ? CollisionPhase::Stay : CollisionPhase::Enter;
            writer.send(CollisionEvent{pair.first, pair.second, phase, is_trigger});
        }
    }

    for (const auto& [pair, was_trigger] : state.last) {
        if (!current.contains(pair)) {
            writer.send(CollisionEvent{pair.first, pair.second, CollisionPhase::Exit, was_trigger});
        }
    }

    state.last = std::move(current);
}

}
