#include "core/frame_step.h"

#include <engine/audio/audio_system.h>
#include <engine/core/time.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>

namespace engine {

void flush_game_events(IGame& game) {
    game.world().flush_events();
}

void simulate_game_frame(IGame& game, IAudioSystem* audio, FixedStepClock& clock, float real_dt) {
    ecs::World& world = game.world();
    Time& time = world.ctx<Time>();
    ui::begin_frame(world);

    const int steps = clock.advance(real_dt);
    if (audio != nullptr) {
        audio->update(time.delta_time);
    }
    for (int i = 0; i < steps; ++i) {
        game.on_fixed_update();
    }
    game.on_update();
}

}
