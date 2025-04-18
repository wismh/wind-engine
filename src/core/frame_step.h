#pragma once

#include <engine/core/fixed_step.h>
#include <engine/igame.h>

namespace engine {

class IAudioSystem;

void flush_game_events(IGame& game);

// UI frame begin, fixed steps, audio update, then on_update. Host and the windowed loop share this
// so the phase order cannot drift. The caller measures real_dt and decides whether to flush.
void simulate_game_frame(IGame& game, IAudioSystem* audio, FixedStepClock& clock, float real_dt);

}
