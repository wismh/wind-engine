#pragma once

#include <engine/core/application_state.h>
#include <engine/core/bound_windows.h>
#include <engine/core/fixed_step.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/presentation.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine {

// The process's simulations. Each world has its own entities, time, and systems.
// Windows and `ApplicationState` stay here, one per process.
class Worlds {
public:
    explicit Worlds(IFatalError& fatal);

    Worlds(const Worlds&) = delete;
    Worlds& operator=(const Worlds&) = delete;

    // Creates a world. Simulation systems are registered once `set_deps` has run.
    [[nodiscard]] ecs::World& add();

    // Drops the world and unbinds its windows. The reference is dead after this returns.
    void destroy(ecs::World& world);

    // One window belongs to one world. Binding it again to a different world is fatal.
    void bind_window(WindowId id, ecs::World& world);
    void unbind_window(WindowId id);
    [[nodiscard]] ecs::World* world_for(WindowId id) const;

    void set_deps(EngineSystemDeps deps);
    void enable_ui(ecs::World& world);
    void enable_audio(ecs::World& world);

    // `false` skips this world's fixed steps and frame. The flag is read by its clock.
    void set_stepping(ecs::World& world, bool stepping);

    // Copies window hosts onto every world that already draws UI, and onto later `enable_ui` calls.
    void set_ui_installer(std::function<void(ecs::World&)> installer);

    void advance_clocks(float real_dt);
    [[nodiscard]] int pending_steps(const ecs::World& world) const;

    [[nodiscard]] ApplicationState& application_state();
    [[nodiscard]] const ApplicationState& application_state() const;
    [[nodiscard]] ui::Presentation& presentation();

    [[nodiscard]] bool has_window(ecs::World& world) const;
    [[nodiscard]] bool draws_ui(const ecs::World& world) const;

    template<typename Fn>
    void each(Fn&& fn) {
        for (const std::unique_ptr<Slot>& slot : slots_) {
            fn(*slot->world, *slot->clock, slot->stepping, slot->ui);
        }
    }

    template<typename Fn>
    void each_world(Fn&& fn) {
        for (const std::unique_ptr<Slot>& slot : slots_) {
            fn(*slot->world);
        }
    }

private:
    struct Slot {
        std::unique_ptr<ecs::World> world;
        bool stepping = true;
        bool simulation = false;
        bool ui = false;
        bool audio = false;
        int pending_steps = 0;
        std::unique_ptr<FixedStepClock> clock;
    };

    [[nodiscard]] Slot* find(ecs::World& world);
    [[nodiscard]] const Slot* find(const ecs::World& world) const;
    void register_simulation(Slot& slot);

    IFatalError* fatal_ = nullptr;
    ApplicationState app_{};
    ui::Presentation presentation_{};
    EngineSystemDeps deps_{};
    bool deps_set_ = false;
    std::function<void(ecs::World&)> ui_installer_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::unordered_map<WindowId, ecs::World*> windows_;
};

}
