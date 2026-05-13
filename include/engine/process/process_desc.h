#pragma once

// docs/tech/modules/Process.md

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// One variable set for the child on top of the parent's environment. Names compare without case on Windows.
struct ProcessVariable {
    std::string name;
    std::string value;
};

// What to start. Strings are UTF-8.
struct ProcessDesc {
    // A path, or a bare name the platform looks up the way its shell would (on Windows: the parent's directory,
    // the current directory, the system directories, then PATH, with `.exe` appended when there is no extension).
    std::filesystem::path program;
    // Passed one by one: the platform quotes each so the child sees exactly these strings.
    std::vector<std::string> arguments;
    // Empty: the parent's current directory.
    std::filesystem::path working_directory;
    // Set on top of the parent's environment. Empty: the parent's environment as is.
    std::vector<ProcessVariable> environment;
};

// The program never ran.
enum class ProcessError {
    // `program` was not found.
    NotFound,
    // The platform refused to start it (access denied, a bad working directory, not an executable).
    StartFailed,
    // No process support on this platform.
    Unsupported,
};

struct ProcessExit {
    // The program's exit code. On Windows a crash is its NTSTATUS, so it may be negative.
    int code = 0;
};

using ProcessResult = std::expected<ProcessExit, ProcessError>;

}
