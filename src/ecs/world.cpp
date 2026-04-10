#include "engine/ecs/world.h"

#include <engine/ecs/hierarchy.h>

#include <algorithm>

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

std::vector<Entity> World::alive_entities() const {
    std::vector<Entity> out;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(alive_.size()); ++i) {
        if (alive_[i] != 0) {
            out.push_back(Entity{i, generations_[i]});
        }
    }
    return out;
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
    // Destroying a parent destroys every descendant. The walk is iterative so a deep chain cannot overflow the stack.
    // A child counts only when its Parent names the entity that lists it, which keeps the walk a finite forest.
    if (const Parent* parent = try_get<Parent>(entity)) {
        if (Children* siblings = try_get<Children>(parent->entity)) {
            auto& list = siblings->entities;
            list.erase(std::remove(list.begin(), list.end(), entity), list.end());
            if (list.empty()) {
                remove<Children>(parent->entity);
            }
        }
    }
    std::vector<Entity> doomed{entity};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        const Children* children = try_get<Children>(doomed[i]);
        if (children == nullptr) {
            continue;
        }
        for (Entity child : children->entities) {
            const Parent* parent = try_get<Parent>(child);
            if (valid(child) && parent != nullptr && parent->entity == doomed[i]) {
                doomed.push_back(child);
            }
        }
    }
    for (Entity victim : doomed) {
        release(victim);
    }
}

void World::release(Entity entity) {
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

void World::add_system(Schedule schedule, Phase phase, SystemFn fn) {
    systems_[schedule_index(schedule)][phase_index(phase)].push_back(std::move(fn));
}

void World::run(Schedule schedule) {
    const auto run_phases = [this, schedule](const auto& phases) {
        auto& by_phase = systems_[schedule_index(schedule)];
        for (const Phase phase : phases) {
            auto& list = by_phase[phase_index(phase)];
            for (std::size_t i = 0; i < list.size(); ++i) {
                list[i](*this);
            }
        }
    };

    if (schedule == Schedule::Fixed) {
        run_phases(kFixedPhases);
    } else {
        run_phases(kFramePhases);
    }
}

void World::flush_events() {
    for (auto& update : event_queues_) {
        update();
    }
}

}
