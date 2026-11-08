#include "physics_3d_internal.h"
#include "physics_sleep.h"

#include <glm/geometric.hpp>

#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] bool is_sleeping_dynamic(const RigidBody3D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && body->sleeping;
}

[[nodiscard]] bool is_awake_dynamic(const RigidBody3D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && !body->sleeping;
}

[[nodiscard]] bool is_moving_kinematic(const RigidBody3D* body) {
    if (body == nullptr || body->type != BodyType::Kinematic) {
        return false;
    }
    return body->velocity != glm::vec3{0.f} || body->angular_velocity != glm::vec3{0.f};
}

void clear_sleep(ecs::World& world, ecs::Entity entity) {
    RigidBody3D* body = world.try_get<RigidBody3D>(entity);
    if (body == nullptr) {
        return;
    }
    body->sleeping = false;
    world.ctx<PhysicsSleepState3D>().quiet.erase(entity);
}

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem3D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem3D& item : colliders) {
        boxes.push_back(aabb_of_3d(item));
    }
    return query_sorted_pairs(boxes);
}

}

void wake_sleepers_by_velocity_3d(ecs::World& world) {
    world.view<RigidBody3D>().each([&](ecs::Entity entity, RigidBody3D& body) {
        if (body.type != BodyType::Dynamic || !body.sleeping) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = glm::length(body.angular_velocity);
        if (sleep_speed_wakes(linear, angular)) {
            body.sleeping = false;
            world.ctx<PhysicsSleepState3D>().quiet.erase(entity);
        }
    });
}

void wake_sleepers_by_overlap_3d(ecs::World& world) {
    const std::vector<ColliderItem3D> colliders = gather_colliders_3d(world);
    std::vector<ecs::Entity> wake;
    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem3D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem3D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || !layers_match_3d(a, b)) {
            continue;
        }
        const RigidBody3D* body_a = world.try_get<RigidBody3D>(a.entity);
        const RigidBody3D* body_b = world.try_get<RigidBody3D>(b.entity);
        const bool wake_a = is_sleeping_dynamic(body_a) && (is_awake_dynamic(body_b) || is_moving_kinematic(body_b));
        const bool wake_b = is_sleeping_dynamic(body_b) && (is_awake_dynamic(body_a) || is_moving_kinematic(body_a));
        if (!wake_a && !wake_b) {
            continue;
        }
        const ContactManifold3D manifold = make_contact_3d(contact_shape_of_3d(a), contact_shape_of_3d(b));
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

void update_sleep_3d(ecs::World& world, std::span<const ecs::Entity> contacting) {
    PhysicsSleepState3D& state = world.ctx<PhysicsSleepState3D>();
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
        const RigidBody3D* body_a = world.try_get<RigidBody3D>(a);
        const RigidBody3D* body_b = world.try_get<RigidBody3D>(b);
        const bool dyn_a = body_a != nullptr && body_a->type == BodyType::Dynamic;
        const bool dyn_b = body_b != nullptr && body_b->type == BodyType::Dynamic;

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
        bool all_quiet = true;
        for (ecs::Entity e : members) {
            const RigidBody3D& body = world.get<RigidBody3D>(e);
            const float linear = glm::length(body.velocity);
            const float angular = glm::length(body.angular_velocity);
            if (sleep_speed_wakes(linear, angular)) {
                all_quiet = false;
                break;
            }
        }

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
                RigidBody3D& body = world.get<RigidBody3D>(e);
                if (next_quiet >= kSleepSteps) {
                    body.sleeping = true;
                    body.velocity = glm::vec3{0.f};
                    body.angular_velocity = glm::vec3{0.f};
                }
                next.emplace(e, next_quiet);
            }
        } else {
            for (ecs::Entity e : members) {
                RigidBody3D& body = world.get<RigidBody3D>(e);
                body.sleeping = false;
            }
        }
    }

    // Dynamic bodies without contacts stay awake and reset quiet
    world.view<RigidBody3D>().each([&](ecs::Entity entity, RigidBody3D& body) {
        if (body.type != BodyType::Dynamic) {
            return;
        }
        if (!dynamic_contacting.contains(entity)) {
            body.sleeping = false;
        }
    });

    state.quiet = std::move(next);
}

}
