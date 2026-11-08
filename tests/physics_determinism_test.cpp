#include <gtest/gtest.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>

#include "ecs/physics_2d_internal.h"
#include "ecs/physics_3d_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct DeterminismBodyId {
    int id{0};
};

[[nodiscard]] std::uint64_t hash_u32(std::uint64_t hash, std::uint32_t value) noexcept {
    for (int byte_index = 0; byte_index < 4; ++byte_index) {
        const std::uint8_t byte = static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xFFu);
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t hash_float(std::uint64_t hash, float value) noexcept {
    const float normalized = (value == 0.f) ? 0.f : value;
    return hash_u32(hash, std::bit_cast<std::uint32_t>(normalized));
}

[[nodiscard]] std::uint64_t hash_vec3(std::uint64_t hash, glm::vec3 v) noexcept {
    hash = hash_float(hash, v.x);
    hash = hash_float(hash, v.y);
    hash = hash_float(hash, v.z);
    return hash;
}

[[nodiscard]] std::uint64_t hash_quat(std::uint64_t hash, glm::quat q) noexcept {
    hash = hash_float(hash, q.x);
    hash = hash_float(hash, q.y);
    hash = hash_float(hash, q.z);
    hash = hash_float(hash, q.w);
    return hash;
}

struct HashEntry2D {
    int sort_id{0};
    engine::ecs::Entity entity{};
    glm::vec3 position{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    float angle_z{0.f};
    glm::vec3 velocity{0.f};
    float angular_velocity{0.f};
    bool sleeping{false};
};

[[nodiscard]] std::uint64_t hash_world_state_2d(engine::ecs::World& world) {
    std::vector<HashEntry2D> entries;
    world.view<engine::Transform, engine::RigidBody2D>().each(
            [&](engine::ecs::Entity entity, const engine::Transform& transform, const engine::RigidBody2D& body) {
                if (body.type != engine::BodyType::Dynamic) {
                    return;
                }
                const DeterminismBodyId* tag = world.try_get<DeterminismBodyId>(entity);
                const int sort_id = tag != nullptr ? tag->id : static_cast<int>(entity.index);
                entries.push_back(HashEntry2D{
                        .sort_id = sort_id,
                        .entity = entity,
                        .position = transform.position,
                        .rotation = transform.rotation,
                        .angle_z = engine::angle_z(transform.rotation),
                        .velocity = body.velocity,
                        .angular_velocity = body.angular_velocity,
                        .sleeping = body.sleeping,
                });
            });

    std::sort(entries.begin(), entries.end(), [](const HashEntry2D& lhs, const HashEntry2D& rhs) {
        if (lhs.sort_id != rhs.sort_id) {
            return lhs.sort_id < rhs.sort_id;
        }
        return lhs.entity < rhs.entity;
    });

    std::uint64_t hash = kFnvOffsetBasis;
    for (const HashEntry2D& entry : entries) {
        hash = hash_vec3(hash, entry.position);
        hash = hash_quat(hash, entry.rotation);
        hash = hash_float(hash, entry.angle_z);
        hash = hash_vec3(hash, entry.velocity);
        hash = hash_float(hash, entry.angular_velocity);
        hash = hash_u32(hash, entry.sleeping ? 1u : 0u);
    }
    return hash;
}

struct HashEntry3D {
    int sort_id{0};
    engine::ecs::Entity entity{};
    glm::vec3 position{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 velocity{0.f};
    glm::vec3 angular_velocity{0.f};
    bool sleeping{false};
};

[[nodiscard]] std::uint64_t hash_world_state_3d(engine::ecs::World& world) {
    std::vector<HashEntry3D> entries;
    world.view<engine::Transform, engine::RigidBody3D>().each(
            [&](engine::ecs::Entity entity, const engine::Transform& transform, const engine::RigidBody3D& body) {
                if (body.type != engine::BodyType::Dynamic) {
                    return;
                }
                const DeterminismBodyId* tag = world.try_get<DeterminismBodyId>(entity);
                const int sort_id = tag != nullptr ? tag->id : static_cast<int>(entity.index);
                entries.push_back(HashEntry3D{
                        .sort_id = sort_id,
                        .entity = entity,
                        .position = transform.position,
                        .rotation = transform.rotation,
                        .velocity = body.velocity,
                        .angular_velocity = body.angular_velocity,
                        .sleeping = body.sleeping,
                });
            });

    std::sort(entries.begin(), entries.end(), [](const HashEntry3D& lhs, const HashEntry3D& rhs) {
        if (lhs.sort_id != rhs.sort_id) {
            return lhs.sort_id < rhs.sort_id;
        }
        return lhs.entity < rhs.entity;
    });

    std::uint64_t hash = kFnvOffsetBasis;
    for (const HashEntry3D& entry : entries) {
        hash = hash_vec3(hash, entry.position);
        hash = hash_quat(hash, entry.rotation);
        hash = hash_vec3(hash, entry.velocity);
        hash = hash_vec3(hash, entry.angular_velocity);
        hash = hash_u32(hash, entry.sleeping ? 1u : 0u);
    }
    return hash;
}

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(engine::ecs::Schedule::Fixed);
}

[[nodiscard]] engine::PolygonCollider2D make_pentagon_collider(float radius, float friction, float restitution) {
    engine::PolygonCollider2D poly{};
    poly.count = 5;
    poly.friction = friction;
    poly.restitution = restitution;
    constexpr std::array<glm::vec2, 5> kUnitVerts = {{
            {1.0f, 0.0f},
            {0.30901699f, 0.95105652f},
            {-0.80901699f, 0.58778525f},
            {-0.80901699f, -0.58778525f},
            {0.30901699f, -0.95105652f},
    }};
    for (std::size_t i = 0; i < kUnitVerts.size(); ++i) {
        poly.vertices[i] = kUnitVerts[i] * radius;
    }
    return poly;
}

void build_complex_scene_2d(engine::ecs::World& world, bool churn_entities = false) {
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -30.f, 0.f};

    // Static basin: floor and two angled/vertical containment walls
    {
        const engine::ecs::Entity floor_entity = world.create();
        world.emplace<engine::Transform>(floor_entity, engine::Transform{.position = glm::vec3{0.f, -1.f, 0.f}});
        world.emplace<engine::BoxCollider2D>(floor_entity, engine::BoxCollider2D{
                                                                   .size = glm::vec3{60.f, 2.f, 1.f},
                                                                   .friction = 0.5f,
                                                                   .restitution = 0.1f,
                                                           });

        const engine::ecs::Entity left_wall = world.create();
        world.emplace<engine::Transform>(left_wall, engine::Transform{.position = glm::vec3{-20.f, 8.f, 0.f}});
        world.emplace<engine::BoxCollider2D>(left_wall, engine::BoxCollider2D{
                                                                .size = glm::vec3{2.f, 20.f, 1.f},
                                                                .friction = 0.4f,
                                                                .restitution = 0.2f,
                                                        });

        const engine::ecs::Entity right_wall = world.create();
        world.emplace<engine::Transform>(right_wall, engine::Transform{.position = glm::vec3{20.f, 8.f, 0.f}});
        world.emplace<engine::BoxCollider2D>(right_wall, engine::BoxCollider2D{
                                                                 .size = glm::vec3{2.f, 20.f, 1.f},
                                                                 .friction = 0.4f,
                                                                 .restitution = 0.2f,
                                                         });
    }

    // 8-row pyramid of mixed boxes, circles, and convex pentagons (36 dynamic bodies)
    constexpr int kRows = 8;
    constexpr float kSpacingX = 1.15f;
    constexpr float kSpacingY = 1.05f;
    int body_id = 0;

    for (int row = 0; row < kRows; ++row) {
        const int cols = kRows - row;
        const float start_x = -0.5f * static_cast<float>(cols - 1) * kSpacingX;
        const float y = 0.6f + static_cast<float>(row) * kSpacingY;

        for (int col = 0; col < cols; ++col) {
            if (churn_entities) {
                for (int spacer = 0; spacer < (body_id % 3) + 1; ++spacer) {
                    (void)world.create();
                }
                for (int cycle = 0; cycle < (body_id % 4) + 1; ++cycle) {
                    const engine::ecs::Entity temp = world.create();
                    world.destroy(temp);
                }
            }

            const float x = start_x + static_cast<float>(col) * kSpacingX + ((row % 2 == 0) ? 0.03f : -0.03f);
            const float angle = ((body_id % 2 == 0) ? 0.08f : -0.08f) * static_cast<float>((body_id % 4) + 1);
            const float friction = 0.25f + 0.1f * static_cast<float>(body_id % 5);
            const float restitution = (body_id % 3 == 0) ? 0.25f : 0.05f;

            const engine::ecs::Entity entity = world.create();
            world.emplace<DeterminismBodyId>(entity, DeterminismBodyId{.id = body_id});
            world.emplace<engine::Transform>(entity, engine::Transform{
                                                             .position = glm::vec3{x, y, 0.f},
                                                             .rotation = engine::rotation_z(angle),
                                                     });
            world.emplace<engine::RigidBody2D>(
                    entity, engine::RigidBody2D{
                                    .velocity = glm::vec3{(col % 2 == 0) ? 0.5f : -0.5f, -1.f, 0.f},
                                    .type = engine::BodyType::Dynamic,
                                    .mass = 1.f + 0.25f * static_cast<float>(body_id % 4),
                                    .angular_velocity = (body_id % 2 == 0) ? 0.3f : -0.3f,
                            });

            if (body_id % 3 == 0) {
                world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{
                                                                     .size = glm::vec3{0.9f, 0.9f, 1.f},
                                                                     .friction = friction,
                                                                     .restitution = restitution,
                                                             });
            } else if (body_id % 3 == 1) {
                world.emplace<engine::CircleCollider2D>(entity, engine::CircleCollider2D{
                                                                        .radius = 0.45f,
                                                                        .friction = friction,
                                                                        .restitution = restitution,
                                                                });
            } else {
                world.emplace<engine::PolygonCollider2D>(entity,
                                                         make_pentagon_collider(0.48f, friction, restitution));
            }
            ++body_id;
        }
    }
}

void build_complex_scene_3d(engine::ecs::World& world) {
    world.ctx<engine::Physics3DConfig>().gravity = glm::vec3{0.f, -30.f, 0.f};

    // Static floor
    const engine::ecs::Entity floor_entity = world.create();
    world.emplace<engine::Transform>(floor_entity, engine::Transform{.position = glm::vec3{0.f, -1.f, 0.f}});
    world.emplace<engine::BoxCollider3D>(floor_entity, engine::BoxCollider3D{
                                                               .size = glm::vec3{60.f, 2.f, 60.f},
                                                               .friction = 0.5f,
                                                               .restitution = 0.1f,
                                                       });

    // 4x4x4 = 64 dynamic 3D bodies (boxes, spheres, capsules)
    int body_id = 0;
    for (int y = 0; y < 4; ++y) {
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                const float px = (static_cast<float>(x) - 1.5f) * 1.35f + ((y % 2 == 0) ? 0.05f : -0.05f);
                const float py = 0.65f + static_cast<float>(y) * 1.1f;
                const float pz = (static_cast<float>(z) - 1.5f) * 1.35f;
                const float friction = 0.3f + 0.1f * static_cast<float>(body_id % 4);
                const float restitution = (body_id % 3 == 0) ? 0.2f : 0.05f;

                const engine::ecs::Entity entity = world.create();
                world.emplace<DeterminismBodyId>(entity, DeterminismBodyId{.id = body_id});
                world.emplace<engine::Transform>(
                        entity, engine::Transform{
                                        .position = glm::vec3{px, py, pz},
                                        .rotation = engine::euler_xyz(glm::vec3{
                                                0.05f * static_cast<float>(x),
                                                0.07f * static_cast<float>(y),
                                                0.04f * static_cast<float>(z),
                                        }),
                                });
                world.emplace<engine::RigidBody3D>(
                        entity, engine::RigidBody3D{
                                        .velocity = glm::vec3{0.2f * static_cast<float>((x % 2) * 2 - 1), -0.5f, 0.f},
                                        .angular_velocity = glm::vec3{0.1f, -0.15f, 0.2f},
                                        .type = engine::BodyType::Dynamic,
                                        .mass = 1.f + 0.2f * static_cast<float>(body_id % 3),
                                });

                if (body_id % 3 == 0) {
                    world.emplace<engine::BoxCollider3D>(entity, engine::BoxCollider3D{
                                                                         .size = glm::vec3{0.9f, 0.9f, 0.9f},
                                                                         .friction = friction,
                                                                         .restitution = restitution,
                                                                 });
                } else if (body_id % 3 == 1) {
                    world.emplace<engine::SphereCollider3D>(entity, engine::SphereCollider3D{
                                                                            .radius = 0.45f,
                                                                            .friction = friction,
                                                                            .restitution = restitution,
                                                                    });
                } else {
                    world.emplace<engine::CapsuleCollider3D>(entity, engine::CapsuleCollider3D{
                                                                             .radius = 0.35f,
                                                                             .height = 0.3f,
                                                                             .friction = friction,
                                                                             .restitution = restitution,
                                                                     });
                }
                ++body_id;
            }
        }
    }
}

void spawn_single_body_2d(engine::ecs::World& world, int id) {
    const float x = (static_cast<float>(id) - 11.5f) * 8.0f;
    const float y = 0.9f + static_cast<float>(id % 5) * 0.8f;
    const float angle = ((id % 2 == 0) ? 0.12f : -0.12f) * static_cast<float>((id % 3) + 1);
    const float friction = 0.3f + 0.1f * static_cast<float>(id % 4);
    const float restitution = (id % 2 == 0) ? 0.3f : 0.1f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<DeterminismBodyId>(entity, DeterminismBodyId{.id = id});
    world.emplace<engine::Transform>(entity, engine::Transform{
                                                     .position = glm::vec3{x, y, 0.f},
                                                     .rotation = engine::rotation_z(angle),
                                             });
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
                                                       .velocity = glm::vec3{0.25f, -2.f, 0.f},
                                                       .type = engine::BodyType::Dynamic,
                                                       .mass = 1.f + 0.5f * static_cast<float>(id % 3),
                                                       .angular_velocity = (id % 2 == 0) ? 0.4f : -0.4f,
                                               });
    if (id % 3 == 0) {
        world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{
                                                             .size = glm::vec3{0.8f, 0.8f, 1.f},
                                                             .friction = friction,
                                                             .restitution = restitution,
                                                     });
    } else if (id % 3 == 1) {
        world.emplace<engine::CircleCollider2D>(entity, engine::CircleCollider2D{
                                                                .radius = 0.4f,
                                                                .friction = friction,
                                                                .restitution = restitution,
                                                        });
    } else {
        world.emplace<engine::PolygonCollider2D>(entity, make_pentagon_collider(0.42f, friction, restitution));
    }
}

struct PhysicsRuntimeSnapshot2D {
    engine::ecs::WorldAsset world_asset{};
    engine::PhysicsContactCache2D contact_cache{};
    engine::PhysicsSleepState2D sleep_state{};
    engine::PhysicsOverlapState overlap_state{};
};

[[nodiscard]] PhysicsRuntimeSnapshot2D capture_snapshot_2d(engine::ecs::World& world,
                                                           const engine::ecs::WorldSerializer& serializer) {
    return PhysicsRuntimeSnapshot2D{
            .world_asset = engine::ecs::capture_world_asset(world, &serializer),
            .contact_cache = world.ctx<engine::PhysicsContactCache2D>(),
            .sleep_state = world.ctx<engine::PhysicsSleepState2D>(),
            .overlap_state = world.ctx<engine::PhysicsOverlapState>(),
    };
}

} // namespace

// Scenario 1: Run a complex multi-body 2D simulation N consecutive times across serial and
// multithreaded worker counts for 100 steps from identical initial states; assert step-by-step
// and final 64-bit FNV-1a state hashes are bit-identical across all runs.
TEST(Determinism, MultiRunIdentical) {
    constexpr int kSteps = 100;
    constexpr std::array<std::uint32_t, 5> kWorkerCounts = {0, 1, 2, 4, 8};

    auto run_simulation = [](std::uint32_t worker_count) {
        engine::JobSystem jobs(worker_count);
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = &jobs});
        build_complex_scene_2d(world);

        std::vector<std::uint64_t> step_hashes;
        step_hashes.reserve(kSteps);
        for (int step = 0; step < kSteps; ++step) {
            step_physics(world);
            step_hashes.push_back(hash_world_state_2d(world));
        }
        return step_hashes;
    };

    const std::vector<std::uint64_t> baseline_hashes = run_simulation(kWorkerCounts[0]);
    ASSERT_EQ(baseline_hashes.size(), static_cast<std::size_t>(kSteps));
    EXPECT_NE(baseline_hashes.front(), baseline_hashes.back());

    for (std::size_t run = 1; run < kWorkerCounts.size(); ++run) {
        const std::vector<std::uint64_t> hashes = run_simulation(kWorkerCounts[run]);
        ASSERT_EQ(hashes.size(), baseline_hashes.size());
        for (int step = 0; step < kSteps; ++step) {
            EXPECT_EQ(hashes[static_cast<std::size_t>(step)], baseline_hashes[static_cast<std::size_t>(step)])
                    << "Hash mismatch on run " << run << " (workers=" << kWorkerCounts[run] << ") at step " << step;
        }
    }
}

// 3D companion to MultiRunIdentical: verifies bit-level reproducibility across serial and
// multithreaded runs for 3D rigid body physics.
TEST(Determinism, MultiRunIdentical3D) {
    constexpr int kSteps = 100;
    constexpr std::array<std::uint32_t, 5> kWorkerCounts = {0, 1, 2, 4, 8};

    auto run_simulation = [](std::uint32_t worker_count) {
        engine::JobSystem jobs(worker_count);
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = &jobs});
        build_complex_scene_3d(world);

        std::vector<std::uint64_t> step_hashes;
        step_hashes.reserve(kSteps);
        for (int step = 0; step < kSteps; ++step) {
            step_physics(world);
            step_hashes.push_back(hash_world_state_3d(world));
        }
        return step_hashes;
    };

    const std::vector<std::uint64_t> baseline_hashes = run_simulation(kWorkerCounts[0]);
    ASSERT_EQ(baseline_hashes.size(), static_cast<std::size_t>(kSteps));
    EXPECT_NE(baseline_hashes.front(), baseline_hashes.back());

    for (std::size_t run = 1; run < kWorkerCounts.size(); ++run) {
        const std::vector<std::uint64_t> hashes = run_simulation(kWorkerCounts[run]);
        ASSERT_EQ(hashes.size(), baseline_hashes.size());
        for (int step = 0; step < kSteps; ++step) {
            EXPECT_EQ(hashes[static_cast<std::size_t>(step)], baseline_hashes[static_cast<std::size_t>(step)])
                    << "3D hash mismatch on run " << run << " (workers=" << kWorkerCounts[run] << ") at step " << step;
        }
    }
}

// Scenario 2: Spawn the exact same dynamic bodies in reverse order AND with interleaved dummy
// entities (created and destroyed, shifting entity indices and generations); verify that the
// canonical state hash is bit-identical across all runs.
TEST(Determinism, OrderIndependentSpawn) {
    constexpr int kBodyCount = 24;
    constexpr int kSteps = 100;

    auto make_floor_world = [] {
        engine::ecs::World world;
        engine::register_engine_systems(world);
        world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -30.f, 0.f};

        const engine::ecs::Entity floor_entity = world.create();
        world.emplace<engine::Transform>(floor_entity, engine::Transform{.position = glm::vec3{0.f, -1.f, 0.f}});
        world.emplace<engine::BoxCollider2D>(floor_entity, engine::BoxCollider2D{
                                                                   .size = glm::vec3{250.f, 2.f, 1.f},
                                                                   .friction = 0.5f,
                                                                   .restitution = 0.2f,
                                                           });
        return world;
    };

    // 1. Forward spawn order (0 .. kBodyCount - 1)
    engine::ecs::World forward_world = make_floor_world();
    for (int id = 0; id < kBodyCount; ++id) {
        spawn_single_body_2d(forward_world, id);
    }

    // 2. Reverse spawn order (kBodyCount - 1 .. 0)
    engine::ecs::World reverse_world = make_floor_world();
    for (int id = kBodyCount - 1; id >= 0; --id) {
        spawn_single_body_2d(reverse_world, id);
    }

    // 3. Interleaved dummy entities created and destroyed in reverse order
    engine::ecs::World interleaved_world = make_floor_world();
    std::vector<engine::ecs::Entity> persistent_dummies;
    for (int i = 0; i < 15; ++i) {
        const engine::ecs::Entity temp = interleaved_world.create();
        interleaved_world.destroy(temp);
    }
    for (int id = kBodyCount - 1; id >= 0; --id) {
        for (int d = 0; d < (id % 3) + 1; ++d) {
            persistent_dummies.push_back(interleaved_world.create());
        }
        const engine::ecs::Entity recycled = interleaved_world.create();
        interleaved_world.destroy(recycled);
        spawn_single_body_2d(interleaved_world, id);
    }
    for (std::size_t i = 0; i < persistent_dummies.size(); i += 2) {
        interleaved_world.destroy(persistent_dummies[i]);
    }

    for (int step = 0; step < kSteps; ++step) {
        step_physics(forward_world);
        step_physics(reverse_world);
        step_physics(interleaved_world);

        const std::uint64_t hash_forward = hash_world_state_2d(forward_world);
        const std::uint64_t hash_reverse = hash_world_state_2d(reverse_world);
        const std::uint64_t hash_interleaved = hash_world_state_2d(interleaved_world);

        EXPECT_EQ(hash_forward, hash_reverse) << "Reverse spawn order diverged at step " << step;
        EXPECT_EQ(hash_forward, hash_interleaved) << "Interleaved dummy entity spawn diverged at step " << step;
    }

    // Also verify a connected 36-body pyramid with interleaved dummy entity churn between every body
    engine::ecs::World clean_pyramid;
    engine::register_engine_systems(clean_pyramid);
    build_complex_scene_2d(clean_pyramid, false);

    engine::ecs::World churned_pyramid;
    engine::register_engine_systems(churned_pyramid);
    build_complex_scene_2d(churned_pyramid, true);

    for (int step = 0; step < kSteps; ++step) {
        step_physics(clean_pyramid);
        step_physics(churned_pyramid);
        EXPECT_EQ(hash_world_state_2d(clean_pyramid), hash_world_state_2d(churned_pyramid))
                << "Churned pyramid diverged at step " << step;
    }
}

// Scenario 3: Simulate 100 steps, capture/serialize world snapshot, simulate 100 more steps
// (to step 200) and record hash; restore the step-100 snapshot, re-simulate 100 steps
// (to step 200), and assert bitwise equality (hash_original_200 == hash_replayed_200).
TEST(Determinism, SaveRestoreRollback) {
    constexpr int kCheckpointStep = 100;
    constexpr int kReplaySteps = 100;

    engine::ecs::WorldSerializer serializer;
    serializer.register_component<DeterminismBodyId>(
            "determinism_body_id",
            [](engine::ecs::ComponentWriter& out, const DeterminismBodyId& c) { out.field("id", c.id); },
            [](engine::ecs::ComponentReader& in, DeterminismBodyId& c) { (void)in.field("id", c.id); });

    engine::ecs::World world;
    engine::register_engine_systems(world);
    build_complex_scene_2d(world);

    // Advance to step 100
    for (int step = 0; step < kCheckpointStep; ++step) {
        step_physics(world);
    }
    const std::uint64_t hash_step_100 = hash_world_state_2d(world);

    // Capture full world state at step 100
    const PhysicsRuntimeSnapshot2D snapshot_100 = capture_snapshot_2d(world, serializer);

    // Advance from step 100 to step 200 and record hashes
    std::vector<std::uint64_t> original_hashes;
    original_hashes.reserve(kReplaySteps);
    for (int step = 0; step < kReplaySteps; ++step) {
        step_physics(world);
        original_hashes.push_back(hash_world_state_2d(world));
    }
    const std::uint64_t hash_original_200 = original_hashes.back();
    EXPECT_NE(hash_step_100, hash_original_200);

    // Restore step-100 snapshot into a fresh world via WorldSerializer::instantiate_world
    engine::ecs::World restored_world;
    engine::register_engine_systems(restored_world);
    restored_world.ctx<engine::Physics2DConfig>() = world.ctx<engine::Physics2DConfig>();

    auto instantiated = engine::ecs::instantiate_world(restored_world, snapshot_100.world_asset, nullptr, &serializer);
    ASSERT_TRUE(instantiated.has_value()) << instantiated.error().message;
    restored_world.ctx<engine::PhysicsContactCache2D>() = snapshot_100.contact_cache;
    restored_world.ctx<engine::PhysicsSleepState2D>() = snapshot_100.sleep_state;
    restored_world.ctx<engine::PhysicsOverlapState>() = snapshot_100.overlap_state;

    EXPECT_EQ(hash_world_state_2d(restored_world), hash_step_100);

    // Re-simulate 100 steps (step 101 .. 200) and verify step-by-step and final bitwise equality
    for (int step = 0; step < kReplaySteps; ++step) {
        step_physics(restored_world);
        const std::uint64_t replayed_hash = hash_world_state_2d(restored_world);
        EXPECT_EQ(replayed_hash, original_hashes[static_cast<std::size_t>(step)])
                << "Save/restore replay hash mismatch at step " << (kCheckpointStep + step + 1);
    }
    const std::uint64_t hash_replayed_200 = hash_world_state_2d(restored_world);
    EXPECT_EQ(hash_original_200, hash_replayed_200);
}

// 3D companion to SaveRestoreRollback: verifies 3D snapshot capture, restoration, and replay.
TEST(Determinism, SaveRestoreRollback3D) {
    constexpr int kCheckpointStep = 100;
    constexpr int kReplaySteps = 100;

    engine::ecs::WorldSerializer serializer;
    serializer.register_component<DeterminismBodyId>(
            "determinism_body_id",
            [](engine::ecs::ComponentWriter& out, const DeterminismBodyId& c) { out.field("id", c.id); },
            [](engine::ecs::ComponentReader& in, DeterminismBodyId& c) { (void)in.field("id", c.id); });

    engine::ecs::World world;
    engine::register_engine_systems(world);
    build_complex_scene_3d(world);

    for (int step = 0; step < kCheckpointStep; ++step) {
        step_physics(world);
    }
    const std::uint64_t hash_step_100 = hash_world_state_3d(world);

    const engine::ecs::WorldAsset asset_100 = engine::ecs::capture_world_asset(world, &serializer);
    const engine::PhysicsContactCache3D cache_100 = world.ctx<engine::PhysicsContactCache3D>();
    const engine::PhysicsSleepState3D sleep_100 = world.ctx<engine::PhysicsSleepState3D>();
    const engine::PhysicsOverlapState3D overlap_100 = world.ctx<engine::PhysicsOverlapState3D>();

    std::vector<std::uint64_t> original_hashes;
    original_hashes.reserve(kReplaySteps);
    for (int step = 0; step < kReplaySteps; ++step) {
        step_physics(world);
        original_hashes.push_back(hash_world_state_3d(world));
    }
    const std::uint64_t hash_original_200 = original_hashes.back();

    engine::ecs::World restored_world;
    engine::register_engine_systems(restored_world);
    restored_world.ctx<engine::Physics3DConfig>() = world.ctx<engine::Physics3DConfig>();

    auto instantiated = engine::ecs::instantiate_world(restored_world, asset_100, nullptr, &serializer);
    ASSERT_TRUE(instantiated.has_value()) << instantiated.error().message;
    restored_world.ctx<engine::PhysicsContactCache3D>() = cache_100;
    restored_world.ctx<engine::PhysicsSleepState3D>() = sleep_100;
    restored_world.ctx<engine::PhysicsOverlapState3D>() = overlap_100;

    EXPECT_EQ(hash_world_state_3d(restored_world), hash_step_100);

    for (int step = 0; step < kReplaySteps; ++step) {
        step_physics(restored_world);
        const std::uint64_t replayed_hash = hash_world_state_3d(restored_world);
        EXPECT_EQ(replayed_hash, original_hashes[static_cast<std::size_t>(step)])
                << "3D save/restore replay hash mismatch at step " << (kCheckpointStep + step + 1);
    }
    const std::uint64_t hash_replayed_200 = hash_world_state_3d(restored_world);
    EXPECT_EQ(hash_original_200, hash_replayed_200);
}
