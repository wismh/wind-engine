#include "engine/ecs/world.h"

namespace engine::ecs {

Entity World::create() {
    if (!free_list_.empty()) {
        const std::uint32_t index = free_list_.back();
        free_list_.pop_back();
        alive_[index] = 1;
        return Entity{index, generations_[index]};
    }
    const std::uint32_t index = static_cast<std::uint32_t>(generations_.size());
    generations_.push_back(1);
    alive_.push_back(1);
    return Entity{index, 1};
}

void World::destroy(Entity entity) {
    if (!valid(entity)) {
        return;
    }
    if (view_depth_ > 0) {
        pending_destroy_.push_back(entity);
        return;
    }
    destroy_immediate(entity);
}

bool World::valid(Entity entity) const {
    return entity.index < alive_.size() && alive_[entity.index] != 0 && generations_[entity.index] == entity.generation;
}

void World::begin_view() {
    ++view_depth_;
}

void World::end_view() {
    --view_depth_;
    if (view_depth_ == 0) {
        flush_destroyed();
    }
}

void World::destroy_immediate(Entity entity) {
    if (!valid(entity)) {
        return;
    }
    for (auto& [type, pool] : pools_) {
        (void) type;
        pool->remove(entity);
    }
    alive_[entity.index] = 0;
    generations_[entity.index] += 1;
    if (generations_[entity.index] == 0) {
        generations_[entity.index] = 1;
    }
    free_list_.push_back(entity.index);
}

void World::flush_destroyed() {
    std::vector<Entity> pending;
    pending.swap(pending_destroy_);
    for (Entity entity : pending) {
        destroy_immediate(entity);
    }
}

}
