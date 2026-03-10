#include "core/frame_step.h"

#include <engine/audio/audio_system.h>
#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <algorithm>

namespace engine {

void flush_worlds(Worlds& worlds) {
    worlds.each_world([](ecs::World& world) { world.flush_events(); });
}

void simulate_worlds(Worlds& worlds, IAudioSystem* audio, float real_dt) {
    ui::reset_pointer_frame(worlds.presentation());
    worlds.each([](ecs::World& world, FixedStepClock&, bool, bool ui) {
        if (ui) {
            ui::begin_frame(world);
        }
    });
    worlds.advance_clocks(real_dt);
    if (audio != nullptr) {
        audio->update(std::clamp(real_dt, 0.0f, kMaxFrameDt));
    }

    const bool paused = worlds.application_state().paused;
    worlds.each([&](ecs::World& world, FixedStepClock&, bool stepping, bool) {
        if (stepping && !paused) {
            const int steps = worlds.pending_steps(world);
            for (int i = 0; i < steps; ++i) {
                world.run(ecs::Schedule::Fixed);
            }
        }
        if (stepping && (!paused || worlds.has_window(world))) {
            world.run(ecs::Schedule::Frame);
        }
    });
}

}
