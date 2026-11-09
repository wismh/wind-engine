#pragma once

// docs/tech/architecture/Principles.md (Deterministic simulation, rule 5)

namespace engine {

// Puts the calling thread's floating-point environment in the one mode engine simulation runs in: IEEE denormals
// (no flush-to-zero, no denormals-are-zero) and round to nearest. WebAssembly has only that mode, so it is the mode
// every target can share. A thread that is already in it pays one control-register read. World::run calls it before
// Schedule::Fixed, and JobSystem workers before every `parallel_for` range batch, because a game, a driver or an
// audio library may change the mode of a thread it runs on.
void set_simulation_float_mode() noexcept;

// True when the calling thread is in that mode. For tests.
[[nodiscard]] bool simulation_float_mode_active() noexcept;

}
