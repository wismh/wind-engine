#include <gtest/gtest.h>

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/log.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <vector>

namespace {

using engine::Children;
using engine::Parent;
using engine::Transform;
using engine::WorldTransform;
using engine::ecs::Entity;
using engine::ecs::KeepWorld;
using engine::ecs::World;

Entity spawn(World& world, Transform transform) {
    const Entity entity = world.create();
    world.emplace<Transform>(entity, transform);
    return entity;
}

void expect_near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], eps) << "column " << c << " row " << r;
        }
    }
}

void expect_near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

bool has_child(World& world, Entity parent, Entity child) {
    const Children* children = world.try_get<Children>(parent);
    return children != nullptr &&
           std::find(children->entities.begin(), children->entities.end(), child) != children->entities.end();
}

}

TEST(Hierarchy, ChainWorldMatrixWithRotationAndScale) {
    World world;
    const Entity a = spawn(world, Transform{{1.f, 2.f, 3.f}, {0.2f, 0.4f, 0.6f}, {2.f, 2.f, 2.f}});
    const Entity b = spawn(world, Transform{{0.5f, -1.f, 0.f}, {0.f, 0.f, glm::half_pi<float>()}, {1.f, 3.f, 1.f}});
    const Entity c = spawn(world, Transform{{4.f, 0.f, 1.f}, {0.1f, 0.f, 0.f}, {1.f, 1.f, 2.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, b, a));
    ASSERT_TRUE(engine::ecs::set_parent(world, c, b));

    const glm::mat4 expected = engine::model_matrix(world.get<Transform>(a)) *
                               engine::model_matrix(world.get<Transform>(b)) *
                               engine::model_matrix(world.get<Transform>(c));
    expect_near(engine::ecs::compute_world_matrix(world, c), expected);

    engine::ecs::update_world_transforms(world);
    expect_near(engine::ecs::world_matrix(world, c), expected);
    expect_near(engine::ecs::world_matrix(world, a), engine::model_matrix(world.get<Transform>(a)));
    EXPECT_EQ(world.try_get<WorldTransform>(a), nullptr);
    expect_near(engine::ecs::world_position(world, c), glm::vec3(expected[3]));
}

TEST(Hierarchy, KeepWorldYesPreservesWorldPosition) {
    World world;
    const Entity parent = spawn(world, Transform{{10.f, -4.f, 2.f}, {0.3f, 0.5f, 1.1f}, {2.f, 2.f, 2.f}});
    const Entity child = spawn(world, Transform{{1.f, 2.f, 3.f}, {0.2f, -0.4f, 0.7f}, {1.f, 1.5f, 1.f}});
    const glm::mat4 before = engine::ecs::compute_world_matrix(world, child);

    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent, KeepWorld::Yes));
    expect_near(glm::vec3(engine::ecs::compute_world_matrix(world, child)[3]), glm::vec3(before[3]));
    // Uniform parent scale: no skew, so the whole matrix is kept.
    expect_near(engine::ecs::compute_world_matrix(world, child), before, 1e-3f);

    engine::ecs::clear_parent(world, child, KeepWorld::Yes);
    expect_near(engine::ecs::compute_world_matrix(world, child), before, 1e-3f);
    EXPECT_EQ(world.try_get<Parent>(child), nullptr);
}

TEST(Hierarchy, KeepWorldNoLeavesLocal) {
    World world;
    const Entity parent = spawn(world, Transform{{10.f, 0.f, 0.f}, {}, {1.f, 1.f, 1.f}});
    const Entity child = spawn(world, Transform{{1.f, 0.f, 0.f}, {}, {1.f, 1.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));
    EXPECT_FLOAT_EQ(world.get<Transform>(child).position.x, 1.f);
    EXPECT_FLOAT_EQ(engine::ecs::compute_world_matrix(world, child)[3].x, 11.f);
}

TEST(Hierarchy, RejectsInvalidSelfAndCycle) {
    World world;
    const Entity a = spawn(world, Transform{});
    const Entity b = spawn(world, Transform{});
    const Entity c = spawn(world, Transform{});
    const Entity dead = world.create();
    world.destroy(dead);

    EXPECT_FALSE(engine::ecs::set_parent(world, a, a));
    EXPECT_FALSE(engine::ecs::set_parent(world, a, dead));
    EXPECT_FALSE(engine::ecs::set_parent(world, dead, a));
    ASSERT_TRUE(engine::ecs::set_parent(world, b, a));
    ASSERT_TRUE(engine::ecs::set_parent(world, c, b));
    EXPECT_FALSE(engine::ecs::set_parent(world, a, c));
    EXPECT_FALSE(engine::ecs::set_parent(world, a, b));
    EXPECT_EQ(world.try_get<Parent>(a), nullptr);
    EXPECT_TRUE(engine::ecs::set_parent(world, c, b)); // same parent again
}

TEST(Hierarchy, RejectsDepthBeyondLimit) {
    World world;
    std::vector<Entity> chain;
    chain.push_back(spawn(world, Transform{}));
    for (int depth = 1; depth <= engine::ecs::kMaxHierarchyDepth; ++depth) {
        chain.push_back(spawn(world, Transform{}));
        ASSERT_TRUE(engine::ecs::set_parent(world, chain.back(), chain[chain.size() - 2])) << depth;
    }
    const Entity too_deep = spawn(world, Transform{});
    EXPECT_FALSE(engine::ecs::set_parent(world, too_deep, chain.back()));
    EXPECT_EQ(world.try_get<Parent>(too_deep), nullptr);

    // A subtree counts too: hanging a 1-deep subtree under the last link would push its leaf past the limit.
    const Entity subtree_root = spawn(world, Transform{});
    const Entity subtree_leaf = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, subtree_leaf, subtree_root));
    EXPECT_FALSE(engine::ecs::set_parent(world, subtree_root, chain[chain.size() - 2]));
    EXPECT_TRUE(engine::ecs::set_parent(world, subtree_root, chain[chain.size() - 3]));
}

TEST(Hierarchy, DestroyCascadesToDescendants) {
    World world;
    const Entity root = spawn(world, Transform{});
    const Entity mid = spawn(world, Transform{});
    const Entity leaf = spawn(world, Transform{});
    const Entity sibling = spawn(world, Transform{});
    const Entity unrelated = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, mid, root));
    ASSERT_TRUE(engine::ecs::set_parent(world, leaf, mid));
    ASSERT_TRUE(engine::ecs::set_parent(world, sibling, root));

    world.destroy(root);
    EXPECT_FALSE(world.valid(root));
    EXPECT_FALSE(world.valid(mid));
    EXPECT_FALSE(world.valid(leaf));
    EXPECT_FALSE(world.valid(sibling));
    EXPECT_TRUE(world.valid(unrelated));
}

TEST(Hierarchy, DestroyOfDeepChainDoesNotOverflow) {
    World world;
    const Entity root = spawn(world, Transform{});
    std::vector<Entity> all{root};
    // Hand-built past the set_parent limit; the cascade is iterative and must cope.
    for (int i = 0; i < 20000; ++i) {
        const Entity child = world.create();
        world.emplace<Parent>(child, Parent{all.back()});
        world.emplace<Children>(all.back(), Children{{child}});
        all.push_back(child);
    }
    world.destroy(root);
    for (Entity entity : all) {
        ASSERT_FALSE(world.valid(entity));
    }
}

TEST(Hierarchy, DestroyInsideViewIsDeferredAndCascades) {
    World world;
    const Entity root = spawn(world, Transform{});
    const Entity child = spawn(world, Transform{});
    const Entity grandchild = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, child, root));
    ASSERT_TRUE(engine::ecs::set_parent(world, grandchild, child));

    int visited = 0;
    {
        auto view = world.view<Transform>();
        for (Entity entity : view) {
            ++visited;
            if (entity == root) {
                world.destroy(root);
                EXPECT_TRUE(world.valid(root));
                EXPECT_TRUE(world.valid(child));
                EXPECT_TRUE(world.valid(grandchild));
            }
        }
    }
    EXPECT_EQ(visited, 3);
    EXPECT_FALSE(world.valid(root));
    EXPECT_FALSE(world.valid(child));
    EXPECT_FALSE(world.valid(grandchild));
}

TEST(Hierarchy, DestroyingChildCleansParentChildren) {
    World world;
    const Entity parent = spawn(world, Transform{});
    const Entity a = spawn(world, Transform{});
    const Entity b = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, a, parent));
    ASSERT_TRUE(engine::ecs::set_parent(world, b, parent));

    world.destroy(a);
    EXPECT_TRUE(world.valid(parent));
    EXPECT_FALSE(has_child(world, parent, a));
    EXPECT_TRUE(has_child(world, parent, b));

    world.destroy(b);
    EXPECT_EQ(world.try_get<Children>(parent), nullptr);
}

TEST(Hierarchy, DeadParentBecomesRoot) {
    engine::log::init();
    World world;
    const Entity parent = spawn(world, Transform{});
    const Entity child = spawn(world, Transform{{3.f, 0.f, 0.f}, {}, {1.f, 1.f, 1.f}});
    // A Parent that points at a dead entity can only come from hand-made data.
    world.emplace<Parent>(child, Parent{parent});
    world.destroy(parent);

    expect_near(engine::ecs::compute_world_matrix(world, child), engine::model_matrix(world.get<Transform>(child)));
    engine::ecs::update_world_transforms(world);
    EXPECT_EQ(world.try_get<Parent>(child), nullptr);
    EXPECT_EQ(world.try_get<WorldTransform>(child), nullptr);
    engine::ecs::update_world_transforms(world); // nothing left to complain about
    EXPECT_NEAR(engine::ecs::world_position(world, child).x, 3.f, 1e-5f);
}

TEST(Hierarchy, SetWorldPositionWritesLocal) {
    World world;
    const Entity parent = spawn(world, Transform{{10.f, 0.f, 0.f}, {0.f, 0.f, glm::half_pi<float>()}, {2.f, 2.f, 2.f}});
    const Entity child = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));

    engine::ecs::set_world_position(world, child, {10.f, 4.f, 0.f});
    // Parent turned 90 degrees and doubled: local (2, 0, 0) lands at (10, 4, 0).
    expect_near(world.get<Transform>(child).position, {2.f, 0.f, 0.f});
    expect_near(glm::vec3(engine::ecs::compute_world_matrix(world, child)[3]), {10.f, 4.f, 0.f});

    const Entity lone = spawn(world, Transform{});
    engine::ecs::set_world_position(world, lone, {1.f, 2.f, 3.f});
    expect_near(world.get<Transform>(lone).position, {1.f, 2.f, 3.f});
}

TEST(Hierarchy, UpdateIsIdempotentAndRebuildsDeletedWorldTransform) {
    World world;
    const Entity a = spawn(world, Transform{{1.f, 0.f, 0.f}, {0.f, 0.5f, 0.f}, {1.f, 2.f, 1.f}});
    const Entity b = spawn(world, Transform{{0.f, 3.f, 0.f}, {0.3f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, b, a));

    engine::ecs::update_world_transforms(world);
    const glm::mat4 first = world.get<WorldTransform>(b).matrix;
    engine::ecs::update_world_transforms(world);
    expect_near(world.get<WorldTransform>(b).matrix, first, 0.f);

    world.remove<WorldTransform>(b);
    engine::ecs::update_world_transforms(world);
    expect_near(world.get<WorldTransform>(b).matrix, first, 0.f);

    // Local edits show up on the next update only.
    world.get<Transform>(a).position.x = 5.f;
    expect_near(engine::ecs::world_matrix(world, b), first, 0.f);
    engine::ecs::update_world_transforms(world);
    EXPECT_NEAR(world.get<WorldTransform>(b).matrix[3].x, first[3].x + 4.f, 1e-4f);
}

TEST(Hierarchy, ClearParentRemovesWorldTransform) {
    World world;
    const Entity a = spawn(world, Transform{{1.f, 0.f, 0.f}, {}, {1.f, 1.f, 1.f}});
    const Entity b = spawn(world, Transform{{0.f, 3.f, 0.f}, {}, {1.f, 1.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, b, a));
    engine::ecs::update_world_transforms(world);
    ASSERT_NE(world.try_get<WorldTransform>(b), nullptr);

    engine::ecs::clear_parent(world, b);
    EXPECT_EQ(world.try_get<Parent>(b), nullptr);
    EXPECT_EQ(world.try_get<Children>(a), nullptr);
    engine::ecs::update_world_transforms(world);
    EXPECT_EQ(world.try_get<WorldTransform>(b), nullptr);
}

TEST(Hierarchy, ReparentMovesChildBetweenChildrenLists) {
    World world;
    const Entity first = spawn(world, Transform{});
    const Entity second = spawn(world, Transform{});
    const Entity child = spawn(world, Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, child, first));
    EXPECT_TRUE(has_child(world, first, child));

    ASSERT_TRUE(engine::ecs::set_parent(world, child, second));
    EXPECT_FALSE(has_child(world, first, child));
    EXPECT_TRUE(has_child(world, second, child));
    EXPECT_EQ(world.get<Parent>(child).entity, second);
    EXPECT_EQ(world.try_get<Children>(first), nullptr);
}
