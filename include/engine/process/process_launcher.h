#pragma once

// docs/tech/modules/Process.md

#include <engine/process/process_call.h>
#include <engine/process/process_desc.h>

#include <expected>
#include <memory>

namespace engine {

// Starts other programs: tools a call owns (`run`, the editor's build) and programs that live on their own
// (`launch`, the launcher starting an editor).
//   - Windows: CreateProcess. A `run` child gets no console window, NUL as standard input, and one pipe for
//              standard output and standard error. It runs in a job object, so `cancel` ends it and every process
//              it started, and when it exits the processes it left behind are ended too.
//   - Others:  every `run` answers ProcessError::Unsupported and `launch` returns it. is_supported() == false.
// Main thread only.
class IProcessLauncher {
public:
    virtual ~IProcessLauncher() = default;

    // Ends every process a pending call still owns.
    virtual void dispose() = 0;

    // Starts `desc` and returns at once. Output and the result land in the returned call during later frames, an
    // error that kept the program from starting (NotFound, StartFailed, Unsupported) on the next one.
    [[nodiscard]] virtual ProcessCall run(ProcessDesc desc) = 0;

    // Starts `desc` as an independent program: no call, no output, not ended by `dispose`, and not inside a job of
    // this process when the platform allows leaving it.
    [[nodiscard]] virtual std::expected<void, ProcessError> launch(const ProcessDesc& desc) = 0;

    [[nodiscard]] virtual bool is_supported() const = 0;
};

class ProcessLauncher final : public IProcessLauncher {
public:
    ProcessLauncher();
    ProcessLauncher(const ProcessLauncher&) = delete;
    ProcessLauncher& operator=(const ProcessLauncher&) = delete;
    ~ProcessLauncher() override;

    void dispose() override;
    [[nodiscard]] ProcessCall run(ProcessDesc desc) override;
    [[nodiscard]] std::expected<void, ProcessError> launch(const ProcessDesc& desc) override;
    [[nodiscard]] bool is_supported() const override;

    // Hands new output and finished processes to their calls, and ends the processes of cancelled calls. The game
    // loop runs it once per frame, before simulation.
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
