#pragma once

// docs/tech/modules/Core.md

namespace engine {

// The process's stop signals, for as long as a dedicated server's loop runs. Handlers do one thing: set a flag the loop
// reads every tick. A second signal has its default effect (it ends the process), so a hung shutdown can be
// interrupted. One instance at a time; constructing one clears the flag.
class ShutdownSignals {
public:
    ShutdownSignals();
    ~ShutdownSignals();

    ShutdownSignals(const ShutdownSignals&) = delete;
    ShutdownSignals& operator=(const ShutdownSignals&) = delete;
};

// A stop signal arrived since the handlers were installed.
[[nodiscard]] bool shutdown_requested() noexcept;

// Delivers a stop through the installed handler: raise(SIGTERM), or on Windows the console handler itself with
// CTRL_C_EVENT (the real event would reach every process on the console). For tests.
void raise_shutdown_signal_for_test();

}
