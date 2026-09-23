# Process

Other programs started from the engine. The editor runs CMake through it to build a game on Play, and the launcher starts an editor. One frontend, `IProcessLauncher`; the backend is chosen at compile time inside `ProcessLauncher`. No `ENGINE_WITH_*` option and no third-party library.

## API

`include/engine/process/process_launcher.h`, `process_call.h`, `process_desc.h`.

| Type | Role |
| --- | --- |
| `ProcessDesc` | `program` (a path, or a bare name looked up like the shell does), `arguments` (each passed as one argv entry), `working_directory` (empty: the parent's), `environment` (`ProcessVariable` name and value set on top of the parent's environment). Strings are UTF-8 |
| `ProcessExit` | `code`, the program's exit code |
| `ProcessError` | The program never ran: `NotFound`, `StartFailed` (refused, or a missing working directory), `Unsupported` |
| `ProcessResult` | `std::expected<ProcessExit, ProcessError>` |
| `ProcessCall` | One child process, owned by whoever started it |
| `IProcessLauncher` | `run(desc) -> ProcessCall`, `launch(desc) -> std::expected<void, ProcessError>`, `dispose`, `is_supported` |
| `ProcessLauncher` | The engine implementation. Adds `poll()`, which only the game loop calls |

`EngineServices::processes` is the launcher. The caller keeps the call and reads it in any system:

```cpp
build_ = services.processes.run({
        .program = "cmake",
        .arguments = {"--build", build_dir, "--config", "DebugGame"},
        .environment = {{"VSLANG", "1033"}},
});

for (std::string& line : build_.take_output()) {
    log_.push_back(std::move(line));
}
if (std::optional<engine::ProcessResult> result = build_.take()) {
    // *result is the exit code or the ProcessError
}
```

### Contract

- `run` returns at once. Output lines and the result reach the call in `ProcessLauncher::poll`, which `GameLoop::tick` runs after `HttpClient::poll` and before `simulate_worlds`. Every system of a frame sees the same state.
- Standard output and standard error are one stream of lines in the order the program wrote them. A line ends at `\n`, and one `\r` before it is dropped. Bytes are kept as written, so a program that does not write UTF-8 gives non-UTF-8 lines. Standard input is empty (`NUL`).
- `take_output()` returns the lines that arrived since the last call. The poll that brings the result also brings every line before it, and lines not taken stay after `take()`.
- `take()` hands the result once. `pending()` is true from `run` until the result arrives or the call is cancelled.
- `cancel()`, destroying a pending call, or move-assigning over it ends the program and every process it started. No line or result arrives afterwards. The processes end in the next poll.
- When the program exits, the processes it left running are ended too (a `start /b` child, an MSBuild node), so they cannot hold the call open and nothing a build started outlives it.
- An error that kept the program from starting answers on the next poll, so there is one path for every outcome.
- `dispose` ends every program a call still owns and leaves those calls empty. `EngineHost` disposes the launcher with the other services.
- `launch` starts an independent program: no call, no output, no console, no inherited handle, not ended by `dispose`. It tries to leave the job this process runs in (a debugger's, a terminal's) so the program outlives it, and starts inside that job when the job does not allow leaving.
- `ProcessCall::resolved(result, output)` builds a finished call, for fakes of `IProcessLauncher`.

### Why a call

Same reason as [Net](Net.md#why-a-call-and-not-an-event): the launcher belongs to the process, not to a world, and a call dies with its owner. In the editor a build started by a panel dies with the panel.

## Backends

| Build | Backend |
| --- | --- |
| `_WIN32` | `CreateProcessW` (`src/process/windows_process.cpp`) |
| Linux and macOS | `fork` and `execve` (`src/process/posix_process.cpp`) |
| Otherwise (Android, Web) | None. `is_supported()` is false, every `run` answers `Unsupported` on the next poll, and `launch` returns it |

### Windows

`run`:

1. A missing `working_directory` is `StartFailed`.
2. One anonymous pipe; its write end is the child's standard output and standard error. `NUL` is standard input. Only those two handles are inheritable, and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` passes only them, so no other handle of the parent leaks into the child.
3. A job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. The child starts `CREATE_SUSPENDED | CREATE_NO_WINDOW`, joins the job, then resumes, so every process it starts is in the job too.
4. The command line is the program in quotes and each argument quoted by the C runtime's rules (`quote_windows_argument`, `src/process/process_command_line.cpp`): only arguments with a space, tab, newline, or quote, or empty ones, are quoted; quotes are escaped and the backslashes before a quote or the closing quote doubled. `lpApplicationName` is null, so a bare `program` is looked up in the parent's directory, the current directory, the system directories, then `PATH`, with `.exe` added.
5. `environment` is merged into `GetEnvironmentStringsW` by name without case (`merge_environment`); a block is passed only when something is set.
6. A reader thread per child reads the pipe, cuts lines (`LineSplitter`), and pushes them to the call's state.

`poll`, per child: a cancelled call ends the job; otherwise, when the process handle is signalled, the exit code is kept and the job is ended (step "processes it left running"). The lines pushed so far move to the call. Once the reader saw the pipe close and the exit is known (or the call was cancelled), the thread is joined and the result is set. Ending the job closes the last write ends, so the reader always finishes.

`ProcessError`: `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, `ERROR_BAD_PATHNAME`, and `ERROR_INVALID_NAME` are `NotFound`; every other failure is `StartFailed`. An exit code is the `DWORD` as `int`, so an NTSTATUS crash code is negative.

`launch`: `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`, no inherited handles, first with `CREATE_BREAKAWAY_FROM_JOB`, again without it on `ERROR_ACCESS_DENIED`.

### Threads

`ProcessCallState` (`src/process/process_call_state.h`) is shared by the call, the launcher, and the reader thread. The reader only calls `push_lines` and `close_output` under the state's mutex. `output`, `result`, and `result_taken` are main-thread only; `deliver_output` moves pushed lines over in the poll and drops them once the call is cancelled. `cancel` is an atomic flag; the launcher acts on it in its next poll.

### Linux and macOS

`run`:

1. A missing `working_directory` is `StartFailed`. A `program` with a directory part is made absolute (the child changes directory before it runs) and must be a regular file the user can execute; a bare name is looked up in the child's `PATH` (the parent's, with `environment` on top), `/usr/local/bin:/usr/bin:/bin` when there is none. Neither found is `NotFound`. This is decided before `fork`, so the call fails on the next poll without a process.
2. Two close-on-exec pipes: the output pipe, and an error pipe the child writes its `errno` to when it cannot reach `exec`. The parent reads the error pipe to its end before `run` returns: nothing means `exec` happened (the pipe closed with it); an `errno` is `NotFound` for `ENOENT` and `ENOTDIR`, `StartFailed` otherwise.
3. `fork`, then in the child only async-signal-safe calls (everything it needs is built before): `setpgid(0, 0)` (a new process group, the Windows job), standard input from `/dev/null`, standard output and standard error `dup2`'d onto the output pipe, `chdir`, `execve`. The parent calls `setpgid` too, so the group exists whichever runs first.
4. `environment` is merged into the parent's `environ` by name, case-sensitively (`merge_environment`).
5. A reader thread per child reads the pipe until it closes, cuts lines (`LineSplitter`), and pushes them to the call's state.

`poll`, per child: a cancelled call sends `SIGKILL` to the group (`kill(-pgid)`); otherwise `waitpid(WNOHANG)` brings the exit, and the group is killed then, which ends the processes the program left behind (a `sleep &`). A process that left the group with `setsid` is not reached. The exit code is `WEXITSTATUS`, or 128 plus the signal that ended the program, as a shell reports it. The result waits for the reader to see the pipe close.

`launch`: two forks, so the program is a child of `init` and leaves no zombie. The grandchild calls `setsid`, puts `/dev/null` on all three standard streams, changes directory, and `execve`s. The same error pipe reports `NotFound` or `StartFailed`.

## Not in scope yet

Standard input, separate standard error, a limit on buffered output.

## Tests

`tests/process_test.cpp`: line cutting, argument quoting and the command line, environment merging, call ownership (`resolved`, `take` once with output kept, cancel and destroy, move-assign), state delivery, and errors on the next poll. On Windows, through `cmd.exe`, and on Linux and macOS through `/bin/sh -c`: lines and exit code, standard error in order, environment, working directory, a missing working directory, a background child (`start /b`, `sleep &`) that does not hold the call open, cancel and `dispose` ending a 30-second child at once, and `launch`. On Linux and macOS also a bare program found on `PATH` with arguments that arrive unchanged, and a signal reported as 128 plus its number.

## See also

- [Core](Core.md)
- [Net](Net.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
