#pragma once

// docs/tech/modules/Process.md

#include <engine/process/process_desc.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {

struct ProcessCallState;

// One child process, owned by whoever started it (`IProcessLauncher::run`). Move-only. Destroying a call that is
// still pending cancels it, so a build started by a panel dies with the panel.
//
// Output and the result become visible during the poll at the start of a frame, so every system of that frame
// sees the same state. The lines of the poll that brings the result come with it. Main thread only.
class ProcessCall {
public:
    ProcessCall() = default;
    // Engine side: `IProcessLauncher` implementations build calls from their own state.
    explicit ProcessCall(std::shared_ptr<ProcessCallState> state);

    ProcessCall(const ProcessCall&) = delete;
    ProcessCall& operator=(const ProcessCall&) = delete;
    ProcessCall(ProcessCall&& other) noexcept = default;
    ProcessCall& operator=(ProcessCall&& other) noexcept;
    ~ProcessCall();

    // A call that already holds `output` and `result`. For fakes of IProcessLauncher.
    [[nodiscard]] static ProcessCall resolved(ProcessResult result, std::vector<std::string> output = {});

    // Started, not cancelled, and no result yet.
    [[nodiscard]] bool pending() const;

    // Lines the program wrote (standard output and standard error in one stream, in the order the program wrote
    // them) that arrived since the last call, without the line break. Empty on an empty or cancelled call.
    [[nodiscard]] std::vector<std::string> take_output();

    // The result once. nullopt while pending, after cancel, on an empty call, and after the first take. Lines not
    // taken yet stay for take_output.
    [[nodiscard]] std::optional<ProcessResult> take();

    // Ends the program and every process it started. No output or result arrives afterwards. An empty or finished
    // call ignores it.
    void cancel();

private:
    std::shared_ptr<ProcessCallState> state_;
};

}
