#pragma once

#include <engine/core/fixed_step.h>
#include <engine/core/worlds.h>

namespace engine {

class IAudioSystem;

void flush_worlds(Worlds& worlds);

// Clears pointer consumption, begins UI frames, advances every world clock, updates audio once,
// then runs Fixed and Frame. The caller flushes and polls.
void simulate_worlds(Worlds& worlds, IAudioSystem* audio, float real_dt);

}
