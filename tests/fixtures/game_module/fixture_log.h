#pragma once

#include <string>
#include <vector>

namespace fixture {

// The fixture game appends a line here, in the `ctx` of the first world in `Worlds`, at construction,
// `on_start`, `on_quit`, and destruction. A test creates this ctx itself first, so the object is built
// by test code and outlives the unloaded module.
struct FixtureLog {
    std::vector<std::string> lines;
};

}
