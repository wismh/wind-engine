#include <engine/core/worlds.h>

#include "ui/dock_runtime.h"

#include <algorithm>

namespace engine {

Worlds::Worlds(IFatalError& fatal) : fatal_(&fatal) {}

ecs::World& Worlds::add() {
    auto slot = std::make_unique<Slot>();
    slot->world = std::make_unique<ecs::World>();
    slot->world->ctx<IFatalError*>() = fatal_;
    slot->clock = std::make_unique<FixedStepClock>(slot->world->ctx<Time>(), app_, slot->stepping);
    ecs::World& world = *slot->world;
    slots_.push_back(std::move(slot));
    if (deps_set_) {
        register_simulation(*slots_.back());
    }
    return world;
}

void Worlds::destroy(ecs::World& world) {
    Slot* const slot = find(world);
    if (slot == nullptr) {
        return;
    }
    if (slot->ui) {
        // Dock float windows are the world's own: they close with it, not stay open unbound with a command buffer
        // whose UI draws point into this world's documents.
        ui::close_world_dock_float_windows(world, deps_);
    }
    const std::vector<WindowId> bound = world.ctx<BoundWindows>().ids;
    for (const WindowId id : bound) {
        windows_.erase(id);
    }
    std::erase_if(slots_, [&](const std::unique_ptr<Slot>& candidate) { return candidate->world.get() == &world; });
}

void Worlds::bind_window(WindowId id, ecs::World& world) {
    Slot* const slot = find(world);
    if (slot == nullptr) {
        return;
    }
    const auto existing = windows_.find(id);
    if (existing != windows_.end() && existing->second != &world) {
        fatal_->report("Window is already bound to a world");
        return;
    }
    if (existing == windows_.end()) {
        windows_.emplace(id, &world);
        world.ctx<BoundWindows>().ids.push_back(id);
    }
    ui::bind_presentation(world, presentation_);
}

void Worlds::unbind_window(WindowId id) {
    const auto existing = windows_.find(id);
    if (existing == windows_.end()) {
        return;
    }
    ecs::World& world = *existing->second;
    windows_.erase(existing);
    std::vector<WindowId>& ids = world.ctx<BoundWindows>().ids;
    std::erase(ids, id);
}

ecs::World* Worlds::world_for(WindowId id) const {
    const auto existing = windows_.find(id);
    return existing == windows_.end() ? nullptr : existing->second;
}

void Worlds::set_deps(EngineSystemDeps deps) {
    deps_ = std::move(deps);
    deps_set_ = true;
    for (const std::unique_ptr<Slot>& slot : slots_) {
        register_simulation(*slot);
    }
}

void Worlds::enable_ui(ecs::World& world) {
    Slot* const slot = find(world);
    if (slot == nullptr || slot->ui) {
        return;
    }
    ui::bind_presentation(world, presentation_);
    register_ui_systems(world, deps_);
    slot->ui = true;
    if (ui_installer_) {
        ui_installer_(world);
    }
}

void Worlds::enable_audio(ecs::World& world) {
    Slot* const slot = find(world);
    if (slot == nullptr || slot->audio) {
        return;
    }
    register_audio_systems(world, deps_);
    slot->audio = true;
}

void Worlds::set_stepping(ecs::World& world, bool stepping) {
    Slot* const slot = find(world);
    if (slot == nullptr) {
        return;
    }
    slot->stepping = stepping;
}

void Worlds::set_ui_installer(std::function<void(ecs::World&)> installer) {
    ui_installer_ = std::move(installer);
    if (!ui_installer_) {
        return;
    }
    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (slot->ui) {
            ui_installer_(*slot->world);
        }
    }
}

void Worlds::advance_clocks(float real_dt) {
    for (const std::unique_ptr<Slot>& slot : slots_) {
        slot->pending_steps = slot->clock->advance(real_dt);
    }
}

int Worlds::pending_steps(const ecs::World& world) const {
    const Slot* const slot = find(world);
    return slot == nullptr ? 0 : slot->pending_steps;
}

ApplicationState& Worlds::application_state() {
    return app_;
}

const ApplicationState& Worlds::application_state() const {
    return app_;
}

ui::Presentation& Worlds::presentation() {
    return presentation_;
}

bool Worlds::has_window(ecs::World& world) const {
    return !world.ctx<BoundWindows>().ids.empty();
}

bool Worlds::draws_ui(const ecs::World& world) const {
    const Slot* const slot = find(world);
    return slot != nullptr && slot->ui;
}

Worlds::Slot* Worlds::find(ecs::World& world) {
    return const_cast<Slot*>(static_cast<const Worlds*>(this)->find(world));
}

const Worlds::Slot* Worlds::find(const ecs::World& world) const {
    for (const std::unique_ptr<Slot>& slot : slots_) {
        if (slot->world.get() == &world) {
            return slot.get();
        }
    }
    fatal_->report("World is not in this process");
    return nullptr;
}

void Worlds::register_simulation(Slot& slot) {
    if (slot.simulation) {
        return;
    }
    register_simulation_systems(*slot.world, deps_);
    slot.simulation = true;
}

}
