#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace fixture {

// Set by a test that wants the fixture game to start a background job in `on_start`. The job sets `started`,
// runs until its call is cancelled, then sets `finished`. Built by test code, so it outlives the module.
struct FixtureJobs {
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
};

// The fixture game appends a line here, in the `ctx` of the first world in `Worlds`, at construction,
// `on_start`, `on_quit`, and destruction. A test creates this ctx itself first, so the object is built
// by test code and outlives the unloaded module.
struct FixtureLog {
    std::vector<std::string> lines;
    std::shared_ptr<FixtureJobs> jobs;
};

struct FixtureComponent {
    std::string label = "default";
    int power = 1;
};

struct FixtureDrop {
    int count = 1;
};

} // namespace fixture
