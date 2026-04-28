# Testing Game Code

Writing fast, reliable automated tests is essential for production games. Wind is architected so gameplay domain logic can be tested in isolation without spinning up graphics backends or windowing systems.

---

## 1. Decoupled Domain Logic

Place pure simulation models, math, state machines, and inventory systems in `src/domain/`. Domain files should **not** include heavy engine headers.

```cpp
// src/domain/inventory.h
#pragma once

#include <vector>
#include <string>

namespace game {

struct Item {
    int id = 0;
    std::string name;
    int quantity = 1;
};

class Inventory {
public:
    bool add(Item item);
    bool remove(int item_id, int quantity = 1);
    [[nodiscard]] int count(int item_id) const;

private:
    std::vector<Item> items_;
};

} // namespace game
```

---

## 2. Writing Unit Tests with GoogleTest

GoogleTest ships in the Wind SDK, so `find_package(Wind)` already gives `GTest::gtest_main`:

```cmake
# CMakeLists.txt
find_package(Wind REQUIRED)
enable_testing()

add_executable(game_tests
    tests/inventory_test.cpp
    src/domain/inventory.cpp
)
target_link_libraries(game_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(game_tests DISCOVERY_MODE PRE_TEST)
```

Write test cases in `tests/inventory_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include <domain/inventory.h>

TEST(InventoryTest, AddsItemsCorrectly) {
    game::Inventory inv;
    EXPECT_TRUE(inv.add({.id = 1, .name = "Health Potion", .quantity = 2}));
    EXPECT_EQ(inv.count(1), 2);
}

TEST(InventoryTest, DecrementsQuantityOnRemove) {
    game::Inventory inv;
    inv.add({.id = 1, .name = "Potion", .quantity = 3});
    EXPECT_TRUE(inv.remove(1, 2));
    EXPECT_EQ(inv.count(1), 1);
}
```

---

## 3. Testing ECS Systems with `GameBase`

To test ECS systems that depend on `ecs::World` without opening a window or initializing OpenGL:

```cpp
#include <gtest/gtest.h>
#include <engine/ecs/world.h>
#include <engine/core/time.h>

TEST(MovementSystemTest, IntegratesVelocity) {
    engine::ecs::World world;

    // Set up mock time
    world.ctx<engine::Time>().delta_time = 0.5f;

    auto entity = world.create();
    world.emplace<Position>(entity, glm::vec2{0.0f, 0.0f});
    world.emplace<Velocity>(entity, glm::vec2{10.0f, 0.0f});

    // Run system directly
    movement_system(world);

    EXPECT_FLOAT_EQ(world.get<Position>(entity).value.x, 5.0f);
}
```

Because `ecs::World` is completely decoupled from rendering and OS windows, ECS tests execute in microseconds.

---

## Next Steps

- Return to the [Quickstart](../getting-started/Quickstart.md) to build your game.
- Review the [Table of Contents](../README.md) for all documentation topics.
