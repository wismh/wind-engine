#include "process/posix_process.h"

#if defined(ENGINE_PROCESS_POSIX)

#include "process/line_splitter.h"
#include "process/process_call_state.h"
#include "process/process_command_line.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if !defined(__APPLE__)
extern char** environ;
#endif

namespace engine {
namespace {

// Owns one file descriptor.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd)
        : fd_(fd) {}
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    ~Fd() {
        reset();
    }

    [[nodiscard]] int get() const {
        return fd_;
    }
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

struct Pipe {
    Fd read;
    Fd write;
};

// A pipe whose two ends close on exec: a child only keeps the ends it dup2s on purpose.
std::optional<Pipe> make_pipe() {
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0) {
        return std::nullopt;
    }
    Pipe pipe{Fd{fds[0]}, Fd{fds[1]}};
    for (const int fd : fds) {
        const int flags = ::fcntl(fd, F_GETFD);
        if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
            return std::nullopt;
        }
    }
    return pipe;
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::vector<std::string> parent_environment() {
#if defined(__APPLE__)
    char** const block = *_NSGetEnviron();
#else
    char** const block = environ;
#endif
    std::vector<std::string> entries;
    for (char** entry = block; entry != nullptr && *entry != nullptr; ++entry) {
        entries.emplace_back(*entry);
    }
    return entries;
}

std::vector<std::string> child_environment(const ProcessDesc& desc) {
    return merge_environment(parent_environment(), desc.environment, false);
}

std::string_view variable_of(const std::vector<std::string>& entries, std::string_view name) {
    for (const std::string& entry : entries) {
        if (entry.size() > name.size() && entry.compare(0, name.size(), name) == 0 && entry[name.size()] == '=') {
            return std::string_view(entry).substr(name.size() + 1);
        }
    }
    return {};
}

bool runnable(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && ::access(path.c_str(), X_OK) == 0;
}

// A path as it is, else a bare name looked up in the child's PATH the way a shell would. Relative paths are made
// absolute first, because the child changes directory before it runs.
std::optional<std::filesystem::path> resolve_program(
        const ProcessDesc& desc, const std::vector<std::string>& environment) {
    if (desc.program.empty()) {
        return std::nullopt;
    }
    if (desc.program.has_parent_path()) {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(desc.program, error);
        return !error && runnable(absolute) ? std::optional(absolute) : std::nullopt;
    }
    std::string_view path = variable_of(environment, "PATH");
    if (path.empty()) {
        path = "/usr/local/bin:/usr/bin:/bin";
    }
    while (!path.empty()) {
        const std::size_t colon = path.find(':');
        const std::string_view directory = path.substr(0, colon);
        const std::filesystem::path candidate =
                (directory.empty() ? std::filesystem::path(".") : std::filesystem::path(directory)) / desc.program;
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(candidate, error);
        if (!error && runnable(absolute)) {
            return absolute;
        }
        if (colon == std::string_view::npos) {
            break;
        }
        path.remove_prefix(colon + 1);
    }
    return std::nullopt;
}

bool working_directory_ok(const ProcessDesc& desc) {
    std::error_code error;
    return desc.working_directory.empty() || std::filesystem::is_directory(desc.working_directory, error);
}

// Everything a child needs, built before fork: between fork and exec only async-signal-safe calls are allowed, and
// that excludes malloc.
struct Launch {
    std::string program;
    std::string directory;
    std::vector<std::string> argument_storage;
    std::vector<std::string> environment_storage;
    std::vector<char*> argv;
    std::vector<char*> envp;
};

std::expected<Launch, ProcessError> prepare(const ProcessDesc& desc) {
    if (!working_directory_ok(desc)) {
        return std::unexpected(ProcessError::StartFailed);
    }
    Launch launch;
    launch.environment_storage = child_environment(desc);
    const std::optional<std::filesystem::path> program = resolve_program(desc, launch.environment_storage);
    if (!program) {
        return std::unexpected(ProcessError::NotFound);
    }
    launch.program = utf8(*program);
    launch.directory = utf8(desc.working_directory);
    launch.argument_storage.push_back(utf8(desc.program));
    launch.argument_storage.insert(launch.argument_storage.end(), desc.arguments.begin(), desc.arguments.end());
    for (std::string& argument : launch.argument_storage) {
        launch.argv.push_back(argument.data());
    }
    launch.argv.push_back(nullptr);
    for (std::string& entry : launch.environment_storage) {
        launch.envp.push_back(entry.data());
    }
    launch.envp.push_back(nullptr);
    return launch;
}

// In the child, after fork: report why it could not become the program, then leave without running destructors.
[[noreturn]] void child_fail(int error_fd) {
    const int error = errno;
    const ssize_t written = ::write(error_fd, &error, sizeof(error));
    (void)written;
    ::_exit(127);
}

// The child's side of run and launch. Never returns.
[[noreturn]] void become(const Launch& launch, int error_fd, int output_fd, bool new_session) {
    if (new_session) {
        ::setsid();
    } else {
        ::setpgid(0, 0);
    }
    const int null_in = ::open("/dev/null", O_RDONLY);
    if (null_in < 0 || ::dup2(null_in, STDIN_FILENO) < 0) {
        child_fail(error_fd);
    }
    if (output_fd >= 0) {
        if (::dup2(output_fd, STDOUT_FILENO) < 0 || ::dup2(output_fd, STDERR_FILENO) < 0) {
            child_fail(error_fd);
        }
    } else {
        const int null_out = ::open("/dev/null", O_WRONLY);
        if (null_out < 0 || ::dup2(null_out, STDOUT_FILENO) < 0 || ::dup2(null_out, STDERR_FILENO) < 0) {
            child_fail(error_fd);
        }
    }
    if (!launch.directory.empty() && ::chdir(launch.directory.c_str()) != 0) {
        child_fail(error_fd);
    }
    ::execve(launch.program.c_str(), launch.argv.data(), launch.envp.data());
    child_fail(error_fd);
}

// What the child reported through the error pipe: nothing when it reached exec (the pipe closed on exec).
std::optional<int> read_child_error(Fd& error_read) {
    int error = 0;
    ssize_t got = 0;
    do {
        got = ::read(error_read.get(), &error, sizeof(error));
    } while (got < 0 && errno == EINTR);
    if (got == static_cast<ssize_t>(sizeof(error))) {
        return error;
    }
    return std::nullopt;
}

ProcessError error_from(int error) {
    return error == ENOENT || error == ENOTDIR ? ProcessError::NotFound : ProcessError::StartFailed;
}

class GroupProcess final : public PosixProcess {
public:
    GroupProcess(pid_t pid, Fd read, std::shared_ptr<ProcessCallState> state)
        : pid_(pid)
        , reader_([read = std::move(read), state = std::move(state)] { read_output(read.get(), *state); }) {}

    ~GroupProcess() override {
        end();
        reap();
        join();
    }

    void end() override {
        if (!ended_) {
            ::kill(-pid_, SIGKILL);
            ended_ = true;
        }
    }

    std::optional<int> poll_exit() override {
        if (exit_code_.has_value()) {
            return exit_code_;
        }
        int status = 0;
        const pid_t done = ::waitpid(pid_, &status, WNOHANG);
        if (done != pid_) {
            return std::nullopt;
        }
        reaped_ = true;
        exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        end();
        return exit_code_;
    }

    void join() override {
        if (reader_.joinable()) {
            reader_.join();
        }
    }

private:
    void reap() {
        if (reaped_) {
            return;
        }
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        reaped_ = true;
    }

    static void read_output(int read_fd, ProcessCallState& state) {
        LineSplitter splitter;
        std::array<char, 4096> buffer{};
        std::vector<std::string> lines;
        for (;;) {
            const ssize_t got = ::read(read_fd, buffer.data(), buffer.size());
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            splitter.feed(std::string_view(buffer.data(), static_cast<std::size_t>(got)), lines);
            if (!lines.empty()) {
                state.push_lines(std::exchange(lines, {}));
            }
        }
        splitter.finish(lines);
        if (!lines.empty()) {
            state.push_lines(std::move(lines));
        }
        state.close_output();
    }

    pid_t pid_;
    std::optional<int> exit_code_;
    bool ended_ = false;
    bool reaped_ = false;
    std::thread reader_;
};

}

std::expected<std::unique_ptr<PosixProcess>, ProcessError> PosixProcess::start(
        const ProcessDesc& desc, std::shared_ptr<ProcessCallState> state) {
    const std::expected<Launch, ProcessError> launch = prepare(desc);
    if (!launch) {
        return std::unexpected(launch.error());
    }
    // One pipe for standard output and standard error, so the lines keep the order the program wrote them in.
    std::optional<Pipe> output = make_pipe();
    std::optional<Pipe> failure = make_pipe();
    if (!output || !failure) {
        return std::unexpected(ProcessError::StartFailed);
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(ProcessError::StartFailed);
    }
    if (pid == 0) {
        become(*launch, failure->write.get(), output->write.get(), false);
    }
    // Whichever of parent and child runs first, the child is a group leader before anything signals the group.
    ::setpgid(pid, pid);
    output->write.reset();
    failure->write.reset();
    if (const std::optional<int> error = read_child_error(failure->read)) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return std::unexpected(error_from(*error));
    }
    return std::make_unique<GroupProcess>(pid, std::move(output->read), std::move(state));
}

std::expected<void, ProcessError> launch_posix_process(const ProcessDesc& desc) {
    const std::expected<Launch, ProcessError> launch = prepare(desc);
    if (!launch) {
        return std::unexpected(launch.error());
    }
    std::optional<Pipe> failure = make_pipe();
    if (!failure) {
        return std::unexpected(ProcessError::StartFailed);
    }
    // Two forks: the first child leaves at once and the program is a child of init, so there is no zombie to reap.
    const pid_t first = ::fork();
    if (first < 0) {
        return std::unexpected(ProcessError::StartFailed);
    }
    if (first == 0) {
        const pid_t second = ::fork();
        if (second < 0) {
            child_fail(failure->write.get());
        }
        if (second == 0) {
            become(*launch, failure->write.get(), -1, true);
        }
        ::_exit(0);
    }
    failure->write.reset();
    int status = 0;
    while (::waitpid(first, &status, 0) < 0 && errno == EINTR) {
    }
    if (const std::optional<int> error = read_child_error(failure->read)) {
        return std::unexpected(error_from(*error));
    }
    return {};
}

}

#endif
