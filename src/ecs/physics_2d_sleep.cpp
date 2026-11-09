#include "physics_2d_internal.h"
#include "physics_owner.h"
#include "physics_sleep.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] bool is_sleeping_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && body->sleeping;
}

[[nodiscard]] bool is_awake_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && !body->sleeping;
}

[[nodiscard]] bool is_moving_kinematic(const RigidBody2D* body) {
    if (body == nullptr || body->type != BodyType::Kinematic) {
        return false;
    }
    return body->velocity != glm::vec3{0.f} || body->angular_velocity != 0.f;
}

void clear_sleep(ecs::World& world, ecs::Entity entity) {
    RigidBody2D* body = world.try_get<RigidBody2D>(entity);
    if (body == nullptr) {
        return;
    }
    body->sleeping = false;
    world.ctx<PhysicsSleepState2D>().quiet.erase(entity);
}

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem2D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem2D& item : colliders) {
        boxes.push_back(aabb_of_2d(item));
    }
    return query_sorted_pairs(boxes);
}

}

void wake_sleepers_by_velocity_2d(ecs::World& world) {
    world.view<RigidBody2D>().each([&](ecs::Entity entity, RigidBody2D& body) {
        if (claimed_by_physics_3d(world, entity) || body.type != BodyType::Dynamic || !body.sleeping) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = std::abs(body.angular_velocity);
        if (sleep_speed_wakes(linear, angular)) {
            body.sleeping = false;
            world.ctx<PhysicsSleepState2D>().quiet.erase(entity);
        }
    });
}

[[nodiscard]] bool has_sleeping_dynamic_2d(ecs::World& world) {
    bool has_sleeping = false;
    world.view<RigidBody2D>().each([&](ecs::Entity entity, const RigidBody2D& body) {
        if (!has_sleeping && body.type == BodyType::Dynamic && body.sleeping && !claimed_by_physics_3d(world, entity)) {
            has_sleeping = true;
        }
    });
    return has_sleeping;
}

void wake_sleepers_by_overlap_2d(ecs::World& world, std::span<const ColliderItem2D> active) {
    if (!has_sleeping_dynamic_2d(world)) {
        return;
    }
    // Only colliders on a body's own entity take part, and those are all active: no static needs gathering.
    std::vector<ColliderItem2D> colliders;
    colliders.reserve(active.size());
    for (const ColliderItem2D& item : active) {
        const RigidBody2D* body = world.try_get<RigidBody2D>(item.entity);
        if (is_sleeping_dynamic(body) || is_awake_dynamic(body) || is_moving_kinematic(body)) {
            colliders.push_back(item);
        }
    }
    if (colliders.empty()) {
        return;
    }
    std::vector<ecs::Entity> wake;
    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || claimed_by_physics_3d(world, a.entity) ||
            claimed_by_physics_3d(world, b.entity) || !layers_match_2d(a, b)) {
            continue;
        }
        const RigidBody2D* body_a = world.try_get<RigidBody2D>(a.entity);
        const RigidBody2D* body_b = world.try_get<RigidBody2D>(b.entity);
        const bool wake_a = is_sleeping_dynamic(body_a) && (is_awake_dynamic(body_b) || is_moving_kinematic(body_b));
        const bool wake_b = is_sleeping_dynamic(body_b) && (is_awake_dynamic(body_a) || is_moving_kinematic(body_a));
        if (!wake_a && !wake_b) {
            continue;
        }
        const ContactManifold2D manifold = make_contact_2d(contact_shape_of_2d(a), contact_shape_of_2d(b));
        if (manifold.count == 0) {
            continue;
        }
        if (wake_a) {
            wake.push_back(a.entity);
        }
        if (wake_b) {
            wake.push_back(b.entity);
        }
    }
    for (ecs::Entity entity : wake) {
        clear_sleep(world, entity);
    }
}

void update_sleep_2d(ecs::World& world, std::span<const ecs::Entity> contacting) {
    PhysicsSleepState2D& state = world.ctx<PhysicsSleepState2D>();
    std::map<ecs::Entity, int> next;

    // Contact islands over dynamic bodies
    std::unordered_map<ecs::Entity, ecs::Entity> parent;
    auto find_root = [&](ecs::Entity x) {
        ecs::Entity root = x;
        while (parent.contains(root) && parent[root] != root) {
            root = parent[root];
        }
        ecs::Entity curr = x;
        while (parent.contains(curr) && parent[curr] != root) {
            ecs::Entity nxt = parent[curr];
            parent[curr] = root;
            curr = nxt;
        }
        return root;
    };
    auto unite = [&](ecs::Entity a, ecs::Entity b) {
        ecs::Entity ra = find_root(a);
        ecs::Entity rb = find_root(b);
        if (ra != rb) {
            parent[rb] = ra;
        }
    };

    std::unordered_set<ecs::Entity> dynamic_contacting;
    const std::size_t pair_count = contacting.size() / 2;
    for (std::size_t i = 0; i < pair_count; ++i) {
        const ecs::Entity a = contacting[2 * i];
        const ecs::Entity b = contacting[2 * i + 1];
        const RigidBody2D* body_a = world.try_get<RigidBody2D>(a);
        const RigidBody2D* body_b = world.try_get<RigidBody2D>(b);
        const bool dyn_a = body_a != nullptr && body_a->type == BodyType::Dynamic && !claimed_by_physics_3d(world, a);
        const bool dyn_b = body_b != nullptr && body_b->type == BodyType::Dynamic && !claimed_by_physics_3d(world, b);

        if (dyn_a) {
            dynamic_contacting.insert(a);
            if (!parent.contains(a)) {
                parent[a] = a;
            }
        }
        if (dyn_b) {
            dynamic_contacting.insert(b);
            if (!parent.contains(b)) {
                parent[b] = b;
            }
        }
        if (dyn_a && dyn_b) {
            unite(a, b);
        }
    }

    std::unordered_map<ecs::Entity, std::vector<ecs::Entity>> islands;
    for (ecs::Entity e : dynamic_contacting) {
        islands[find_root(e)].push_back(e);
    }

    // Evaluate sleep per island
    for (const auto& [root, members] : islands) {
        const int member_count = static_cast<int>(members.size());
        const int allowed_waking = member_count > 50 ? std::max(1, member_count / 50) : 0;
        int waking = 0;
        for (ecs::Entity e : members) {
            const RigidBody2D& body = world.get<RigidBody2D>(e);
            const float linear = glm::length(body.velocity);
            const float angular = std::abs(body.angular_velocity);
            if (sleep_speed_wakes(linear, angular)) {
                ++waking;
                if (waking > allowed_waking) {
                    break;
                }
            }
        }
        const bool all_quiet = (waking <= allowed_waking);

        if (all_quiet) {
            int min_quiet = kSleepSteps;
            for (ecs::Entity e : members) {
                int count = 0;
                if (const auto found = state.quiet.find(e); found != state.quiet.end()) {
                    count = found->second;
                }
                min_quiet = std::min(min_quiet, count);
            }
            const int next_quiet = std::min(min_quiet + 1, kSleepSteps);
            for (ecs::Entity e : members) {
                RigidBody2D& body = world.get<RigidBody2D>(e);
                if (next_quiet >= kSleepSteps) {
                    body.sleeping = true;
                    body.velocity = glm::vec3{0.f};
                    body.angular_velocity = 0.f;
                }
                next.emplace(e, next_quiet);
            }
        } else {
            for (ecs::Entity e : members) {
                RigidBody2D& body = world.get<RigidBody2D>(e);
                body.sleeping = false;
            }
        }
    }

    // Dynamic bodies without contacts stay awake and reset quiet
    world.view<RigidBody2D>().each([&](ecs::Entity entity, RigidBody2D& body) {
        if (claimed_by_physics_3d(world, entity) || body.type != BodyType::Dynamic) {
            return;
        }
        if (!dynamic_contacting.contains(entity)) {
            body.sleeping = false;
        }
    });

    state.quiet = std::move(next);
}

}
