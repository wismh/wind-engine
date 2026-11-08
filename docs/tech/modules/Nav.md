# Navigation (`engine::nav`)

Wind Engine navigation subsystem for 2D and 3D pathfinding, crowd steering fields, discrete grid adapters, and hierarchical zone routing.

Public headers live under `<engine/nav/...>`.

## Core Subsystems

| Header | Description |
| --- | --- |
| `<engine/nav/nav_types.h>` | Pathfinding types: `NavPath<Node>`, `PathStatus`, `NavSearchParams`, and `Heuristic` helpers |
| `<engine/nav/a_star.h>` | Generic, template/concept-based budgeted A* search with partial path fallback |
| `<engine/nav/string_puller.h>` | Discrete Bresenham / DDA line-of-sight checks and path smoothing |
| `<engine/nav/grid_graph_2d.h>` | 2D tilemap and grid graph adapter with 4/8-way movement and corner clearance |
| `<engine/nav/grid_graph_3d.h>` | 3D stepped voxel/block world graph with step-up and drop-down limits |
| `<engine/nav/flow_field_2d.h>` | 2D vector field built via reverse Dijkstra with $O(K)$ `touched_` recycling for RTS/swarms |
| `<engine/nav/flow_field_3d.h>` | 3D vector field supporting vertical step-ups and drops |
| `<engine/nav/portal_graph.h>` | Hierarchical Pathfinding (HPA*) across rooms, sectors, and connecting portals |
| `<engine/nav/nav_agent.h>` | ECS component holding waypoints, target, and outputting `desired_velocity` |
| `<engine/nav/nav_system.h>` | ECS system running on `Schedule::Fixed` / `Phase::Game` |

## Usage Examples

### 1. A* Pathfinding over a 2D Grid / Tilemap

```cpp
#include <engine/nav/a_star.h>
#include <engine/nav/grid_graph_2d.h>

auto is_walkable = [](glm::ivec2 cell) {
    return !my_tilemap.is_solid(cell);
};

engine::nav::GridGraph2D graph(is_walkable, /*diagonals=*/true, /*cut_corners=*/false);
graph.set_bounds({0, 0}, {64, 64});

const auto path = engine::nav::a_star_find_path(graph, {1, 1}, {20, 20});
if (path.complete()) {
    // Path found
}
```

### 2. Path Smoothing (String Pulling)

```cpp
#include <engine/nav/string_puller.h>

// Smooths discrete grid zigzags into straight line-of-sight segments:
const std::vector<glm::ivec2> smoothed = engine::nav::smooth_path_2d(path.nodes, is_walkable);
```

### 3. Mass Crowd Navigation (Flow Fields)

```cpp
#include <engine/nav/flow_field_2d.h>

engine::nav::FlowField2D flow;
flow.init_grid({0, 0}, {100, 100});

// Recomputes outward from target goals with zero heap reallocations:
flow.build(target_goals, is_walkable, /*max_range=*/50.0f);

// Each agent queries in O(1):
glm::vec2 steer = flow.direction(agent_cell);
```

### 4. ECS Agent Integration

```cpp
#include <engine/nav/nav_agent.h>
#include <engine/ecs/transform.h>

auto& agent = world.emplace<engine::nav::NavAgent>(entity);
agent.desired_speed = 4.0f;
agent.set_waypoints(path_waypoints);

// In your character movement / physics controller:
const glm::vec3 vel = agent.desired_velocity;
```
