#include <gtest/gtest.h>

#include <engine/ecs/world.h>

#include <vector>

namespace {

struct A {
    int value = 0;
};

struct B {
    int value = 0;
};

}

TEST(Ecs, GenerationalEntity) {
    engine::ecs::World world;
    const engine::ecs::Entity a = world.create();
    world.emplace<A>(a, A{1});

    world.destroy(a);
    const engine::ecs::Entity b = world.create();

    EXPECT_NE(a, b);
    EXPECT_EQ(a.index, b.index);
    EXPECT_NE(a.generation, b.generation);
    EXPECT_FALSE(world.valid(a));
    EXPECT_TRUE(world.valid(b));
    EXPECT_EQ(world.try_get<A>(a), nullptr);
    EXPECT_EQ(world.try_get<A>(b), nullptr);

    world.emplace<A>(b, A{2});
    ASSERT_NE(world.try_get<A>(b), nullptr);
    EXPECT_EQ(world.try_get<A>(b)->value, 2);
}

TEST(Ecs, TryGetAfterDestroyIsEmpty) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<A>(entity, A{7});
    ASSERT_NE(world.try_get<A>(entity), nullptr);

    world.destroy(entity);

    EXPECT_EQ(world.try_get<A>(entity), nullptr);
    EXPECT_FALSE(world.valid(entity));
}

TEST(Ecs, ViewAB) {
    engine::ecs::World world;
    const engine::ecs::Entity only_a = world.create();
    const engine::ecs::Entity only_b = world.create();
    const engine::ecs::Entity both = world.create();
    const engine::ecs::Entity both_too = world.create();

    world.emplace<A>(only_a, A{1});
    world.emplace<B>(only_b, B{2});
    world.emplace<A>(both, A{10});
    world.emplace<B>(both, B{20});
    world.emplace<A>(both_too, A{30});
    world.emplace<B>(both_too, B{40});

    std::vector<engine::ecs::Entity> seen;
    auto view = world.view<A, B>();
    for (engine::ecs::Entity entity : view) {
        seen.push_back(entity);
        view.get<A>(entity).value += 1;
        view.get<B>(entity).value += 2;
    }

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], both);
    EXPECT_EQ(seen[1], both_too);

    EXPECT_EQ(world.get<A>(only_a).value, 1);
    EXPECT_EQ(world.get<B>(only_b).value, 2);
    EXPECT_EQ(world.get<A>(both).value, 11);
    EXPECT_EQ(world.get<B>(both).value, 22);
    EXPECT_EQ(world.get<A>(both_too).value, 31);
    EXPECT_EQ(world.get<B>(both_too).value, 42);
}

TEST(Ecs, DeferredDestroyDuringIteration) {
    engine::ecs::World world;
    const engine::ecs::Entity first = world.create();
    const engine::ecs::Entity second = world.create();
    const engine::ecs::Entity third = world.create();
    world.emplace<A>(first, A{1});
    world.emplace<A>(second, A{2});
    world.emplace<A>(third, A{3});

    std::vector<engine::ecs::Entity> visited;
    for (engine::ecs::Entity entity : world.view<A>()) {
        visited.push_back(entity);
        if (entity == first) {
            world.destroy(first);
            world.destroy(second);
        }
    }

    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], first);
    EXPECT_EQ(visited[1], second);
    EXPECT_EQ(visited[2], third);

    EXPECT_EQ(world.try_get<A>(first), nullptr);
    EXPECT_EQ(world.try_get<A>(second), nullptr);
    ASSERT_NE(world.try_get<A>(third), nullptr);
    EXPECT_EQ(world.try_get<A>(third)->value, 3);
    EXPECT_FALSE(world.valid(first));
    EXPECT_FALSE(world.valid(second));
    EXPECT_TRUE(world.valid(third));

    const engine::ecs::Entity extra = world.create();
    world.emplace<A>(extra, A{4});
    world.destroy(extra);
    EXPECT_EQ(world.try_get<A>(extra), nullptr);
    EXPECT_FALSE(world.valid(extra));
}
