#pragma once

#include <engine/log.h>
#include <engine/resources/fatal_error.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace engine {

// The fatal exit of a dedicated server: one line in the log and exit code 1. Not `abort`, whose dialog would wait
// forever on a machine nobody looks at, and not a message box. Not `std::exit` either: it would run static destructors
// while job workers are still in game code, and joining them is impossible from a worker, where a report may come from.
// The process ends here, so `on_quit` does not run.
class ServerFatalError final : public IFatalError {
public:
    [[noreturn]] void report(std::string_view message) override {
        log::error(message);
        std::fflush(nullptr);
        std::_Exit(1);
    }
};

}
