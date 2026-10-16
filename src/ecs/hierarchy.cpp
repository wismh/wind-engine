#include <engine/ecs/hierarchy.h>

#include <engine/ecs/transform.h>
#include <engine/log.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace engine::ecs {

namespace {

glm::mat4 local_matrix(const World& world, Entity entity) {
    const Transform* transform = world.try_get<Transform>(entity);
    return transform != nullptr ? model_matrix(*transform) : glm::mat4(1.0f);
}

// Parent of `entity` if it has a Parent component naming a live entity, else an invalid entity.
Entity live_parent(const World& world, Entity entity) {
    const Parent* parent = world.try_get<Parent>(entity);
    if (parent == nullptr || !world.valid(parent->entity)) {
        return Entity{};
    }
    return parent->entity;
}

// Longest downward chain below `entity` (0 for a leaf). Bounded, so a hand-made loop cannot hang it.
int height_of(const World& world, Entity entity) {
    int height = 0;
    std::vector<std::pair<Entity, int>> stack{{entity, 0}};
    while (!stack.empty()) {
        const auto [node, level] = stack.back();
        stack.pop_back();
        height = std::max(height, level);
        if (level > kMaxHierarchyDepth) {
            break;
        }
        const Children* children = world.try_get<Children>(node);
        if (children == nullptr) {
            continue;
        }
        for (Entity child : children->entities) {
            stack.emplace_back(child, level + 1);
        }
    }
    return height;
}

void detach_from_parent(World& world, Entity child, Entity parent) {
    Children* siblings = world.try_get<Children>(parent);
    if (siblings == nullptr) {
        return;
    }
    auto& list = siblings->entities;
    list.erase(std::remove(list.begin(), list.end(), child), list.end());
    if (list.empty()) {
        world.remove<Children>(parent);
    }
}

Transform decompose(const glm::mat4& matrix) {
    Transform transform;
    transform.position = glm::vec3(matrix[3]);
    glm::vec3 axes[3] = {glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2])};
    transform.scale = {glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2])};
    for (int i = 0; i < 3; ++i) {
        if (transform.scale[i] > 1e-8f) {
            axes[i] /= transform.scale[i];
        }
    }
    if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) {
        // Mirrored: the rotation must stay proper, so the sign goes to the x scale.
        transform.scale.x = -transform.scale.x;
        axes[0] = -axes[0];
    }
    // With skew (non-uniform scale above a rotation) the axes are not orthogonal. Best fit: keep the X axis, make Y
    // orthogonal to it, and derive Z from both. The frame is already proper, so the cross product keeps its handedness.
    // A zero-length axis (zero scale) has no direction to keep; the axes are used as they are.
    const glm::vec3 y_axis = axes[1] - glm::dot(axes[1], axes[0]) * axes[0];
    const float y_length = glm::length(y_axis);
    if (std::abs(transform.scale.x) > 1e-8f && y_length > 1e-8f) {
        axes[1] = y_axis / y_length;
        axes[2] = glm::cross(axes[0], axes[1]);
    }
    transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
    return transform;
}

void store_local(World& world, Entity entity, const Transform& local) {
    if (Transform* transform = world.try_get<Transform>(entity)) {
        *transform = local;
    } else {
        world.emplace<Transform>(entity, local);
    }
}

}

bool set_parent(World& world, Entity child, Entity parent, KeepWorld keep) {
    if (!world.valid(child) || !world.valid(parent) || child == parent) {
        return false;
    }
    const Parent* current = world.try_get<Parent>(child);
    if (current != nullptr && current->entity == parent) {
        return true;
    }

    // `child` must not be an ancestor of `parent`, and the deepest result must fit.
    int parent_depth = 0;
    for (Entity up = live_parent(world, parent); world.valid(up); up = live_parent(world, up)) {
        if (up == child || ++parent_depth > kMaxHierarchyDepth) {
            return false;
        }
    }
    if (parent_depth + 1 + height_of(world, child) > kMaxHierarchyDepth) {
        return false;
    }

    const glm::mat4 child_world = keep == KeepWorld::Yes ? compute_world_matrix(world, child) : glm::mat4(1.0f);

    if (current != nullptr) {
        detach_from_parent(world, child, current->entity);
        world.get<Parent>(child).entity = parent;
    } else {
        world.emplace<Parent>(child, Parent{parent});
    }
    if (Children* children = world.try_get<Children>(parent)) {
        children->entities.push_back(child);
    } else {
        world.emplace<Children>(parent, Children{{child}});
    }

    if (keep == KeepWorld::Yes) {
        store_local(world, child, decompose(glm::inverse(compute_world_matrix(world, parent)) * child_world));
    }
    return true;
}

void clear_parent(World& world, Entity child, KeepWorld keep) {
    if (!world.valid(child)) {
        return;
    }
    const Parent* parent = world.try_get<Parent>(child);
    if (parent == nullptr) {
        return;
    }
    const glm::mat4 child_world = keep == KeepWorld::Yes ? compute_world_matrix(world, child) : glm::mat4(1.0f);
    detach_from_parent(world, child, parent->entity);
    world.remove<Parent>(child);
    world.remove<WorldTransform>(child);
    if (keep == KeepWorld::Yes) {
        store_local(world, child, decompose(child_world));
    }
}

glm::mat4 world_matrix(const World& world, Entity entity) {
    if (const WorldTransform* cached = world.try_get<WorldTransform>(entity)) {
        return cached->matrix;
    }
    return local_matrix(world, entity);
}

glm::mat4 compute_world_matrix(const World& world, Entity entity) {
    glm::mat4 result = local_matrix(world, entity);
    int steps = 0;
    for (Entity up = live_parent(world, entity); world.valid(up); up = live_parent(world, up)) {
        if (++steps > kMaxHierarchyDepth) {
            break; // a hand-made loop; set_parent never builds one
        }
        result = local_matrix(world, up) * result;
    }
    return result;
}

glm::vec3 world_position(const World& world, Entity entity) {
    return glm::vec3(world_matrix(world, entity)[3]);
}

void set_world_position(World& world, Entity entity, glm::vec3 position) {
    Transform* transform = world.valid(entity) ? world.try_get<Transform>(entity) : nullptr;
    if (transform == nullptr) {
        return;
    }
    const Entity parent = live_parent(world, entity);
    if (!world.valid(parent)) {
        transform->position = position;
        return;
    }
    const glm::mat4 parent_world = compute_world_matrix(world, parent);
    transform->position = glm::vec3(glm::inverse(parent_world) * glm::vec4(position, 1.0f));
}

void update_world_transforms(World& world) {
    std::vector<Entity> dead_parents;
    std::vector<Entity> stale;
    std::vector<Entity> roots;
    for (Entity entity : world.view<Parent>()) {
        if (!world.valid(world.get<Parent>(entity).entity)) {
            dead_parents.push_back(entity);
        }
    }
    for (Entity entity : dead_parents) {
        log::warn("hierarchy: the Parent of entity " + std::to_string(entity.index) +
                  " was destroyed, treating the entity as a root");
        world.remove<Parent>(entity);
    }
    for (Entity entity : world.view<WorldTransform>()) {
        if (world.try_get<Parent>(entity) == nullptr) {
            stale.push_back(entity);
        }
    }
    for (Entity entity : stale) {
        world.remove<WorldTransform>(entity);
    }
    for (Entity entity : world.view<Children>()) {
        if (world.try_get<Parent>(entity) == nullptr) {
            roots.push_back(entity);
        }
    }

    // A child is followed only when its Parent names the node that lists it. Each entity has one Parent, so the walk
    // follows a forest and always ends.
    struct Node {
        Entity entity;
        glm::mat4 world;
    };
    std::vector<Node> stack;
    for (Entity root : roots) {
        stack.push_back({root, local_matrix(world, root)});
        while (!stack.empty()) {
            const Node node = stack.back();
            stack.pop_back();
            const Children* children = world.try_get<Children>(node.entity);
            if (children == nullptr) {
                continue;
            }
            for (Entity child : children->entities) {
                const Parent* parent = world.try_get<Parent>(child);
                if (parent == nullptr || parent->entity != node.entity) {
                    continue;
                }
                const glm::mat4 matrix = node.world * local_matrix(world, child);
                if (WorldTransform* cached = world.try_get<WorldTransform>(child)) {
                    cached->matrix = matrix;
                } else {
                    world.emplace<WorldTransform>(child, WorldTransform{matrix});
                }
                stack.push_back({child, matrix});
            }
        }
    }
}

}
