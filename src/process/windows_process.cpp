#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "process/windows_process.h"

#if defined(_WIN32)

#include "process/line_splitter.h"
#include "process/process_call_state.h"
#include "process/process_command_line.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace engine {
namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

// Owns one kernel handle. Both null and INVALID_HANDLE_VALUE mean none.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle)
        : handle_(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    ~Handle() {
        reset();
    }

    [[nodiscard]] HANDLE get() const {
        return handle_;
    }
    [[nodiscard]] explicit operator bool() const {
        return handle_ != nullptr;
    }
    void reset() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_ = nullptr;
};

ProcessError error_from(DWORD code) {
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_NAME:
        return ProcessError::NotFound;
    default:
        return ProcessError::StartFailed;
    }
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// A missing working directory is StartFailed whatever error CreateProcess would report for it.
bool working_directory_ok(const ProcessDesc& desc) {
    std::error_code error;
    return desc.working_directory.empty() || std::filesystem::is_directory(desc.working_directory, error);
}

std::wstring command_line(const ProcessDesc& desc) {
    return widen(windows_command_line(utf8(desc.program), desc.arguments));
}

// The parent's environment with `desc.environment` on top, as a CREATE_UNICODE_ENVIRONMENT block. Empty when
// nothing is set: the child then inherits the parent's block.
std::wstring environment_block(const ProcessDesc& desc) {
    if (desc.environment.empty()) {
        return {};
    }
    std::vector<std::string> entries;
    if (wchar_t* const strings = GetEnvironmentStringsW(); strings != nullptr) {
        for (const wchar_t* entry = strings; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
            entries.push_back(narrow(entry));
        }
        FreeEnvironmentStringsW(strings);
    }
    std::wstring block;
    for (const std::string& entry : merge_environment(std::move(entries), desc.environment, true)) {
        block += widen(entry);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

class JobProcess final : public WindowsProcess {
public:
    JobProcess(Handle process, Handle job, Handle read, std::shared_ptr<ProcessCallState> state)
        : process_(std::move(process))
        , job_(std::move(job))
        , reader_([read = std::move(read), state = std::move(state)] { read_output(read.get(), *state); }) {}

    ~JobProcess() override {
        end();
        join();
    }

    void end() override {
        if (!ended_) {
            TerminateJobObject(job_.get(), 1);
            ended_ = true;
        }
    }

    std::optional<int> poll_exit() override {
        if (exit_code_.has_value() || WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0) {
            return exit_code_;
        }
        DWORD code = 0;
        GetExitCodeProcess(process_.get(), &code);
        exit_code_ = static_cast<int>(code);
        end();
        return exit_code_;
    }

    void join() override {
        if (reader_.joinable()) {
            reader_.join();
        }
    }

private:
    static void read_output(HANDLE read, ProcessCallState& state) {
        LineSplitter splitter;
        std::array<char, 4096> buffer{};
        std::vector<std::string> lines;
        DWORD got = 0;
        while (ReadFile(read, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) && got > 0) {
            splitter.feed(std::string_view(buffer.data(), got), lines);
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

    Handle process_;
    Handle job_;
    std::optional<int> exit_code_;
    bool ended_ = false;
    std::thread reader_;
};

}

std::expected<std::unique_ptr<WindowsProcess>, ProcessError> WindowsProcess::start(
        const ProcessDesc& desc, std::shared_ptr<ProcessCallState> state) {
    if (!working_directory_ok(desc)) {
        return std::unexpected(ProcessError::StartFailed);
    }

    // One pipe for standard output and standard error, so the lines keep the order the program wrote them in.
    // Only its write end and NUL are inheritable, and only those two reach the child (handle list below).
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE};
    HANDLE read_raw = nullptr;
    HANDLE write_raw = nullptr;
    if (!CreatePipe(&read_raw, &write_raw, &inherit, 0)) {
        return std::unexpected(ProcessError::StartFailed);
    }
    Handle read{read_raw};
    Handle write{write_raw};
    SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0);
    Handle nul{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
            nullptr)};
    if (!nul) {
        return std::unexpected(ProcessError::StartFailed);
    }

    // Closing the job's last handle ends whatever still runs in it, so nothing outlives the launcher.
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    if (!job) {
        return std::unexpected(ProcessError::StartFailed);
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    std::vector<std::byte> list_storage(list_size);
    auto* const list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &list_size)) {
        return std::unexpected(ProcessError::StartFailed);
    }
    std::array<HANDLE, 2> inherited{write.get(), nul.get()};
    const BOOL listed = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
            sizeof(inherited), nullptr, nullptr);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul.get();
    startup.StartupInfo.hStdOutput = write.get();
    startup.StartupInfo.hStdError = write.get();
    startup.lpAttributeList = list;

    std::wstring command = command_line(desc);
    std::wstring environment = environment_block(desc);
    const std::wstring directory = desc.working_directory.wstring();
    PROCESS_INFORMATION info{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    const BOOL created = listed && CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, flags,
            environment.empty() ? nullptr : environment.data(), directory.empty() ? nullptr : directory.c_str(),
            &startup.StartupInfo, &info);
    const DWORD error = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!created) {
        return std::unexpected(listed ? error_from(error) : ProcessError::StartFailed);
    }
    Handle process{info.hProcess};
    const Handle thread{info.hThread};
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        TerminateProcess(process.get(), 1);
        return std::unexpected(ProcessError::StartFailed);
    }
    ResumeThread(thread.get());

    // The child holds its own copies now. With ours closed, the pipe ends when the last process in the job does.
    write.reset();
    nul.reset();
    return std::make_unique<JobProcess>(std::move(process), std::move(job), std::move(read), std::move(state));
}

std::expected<void, ProcessError> launch_windows_process(const ProcessDesc& desc) {
    if (!working_directory_ok(desc)) {
        return std::unexpected(ProcessError::StartFailed);
    }
    std::wstring command = command_line(desc);
    std::wstring environment = environment_block(desc);
    const std::wstring directory = desc.working_directory.wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
    const auto create = [&](DWORD extra) {
        return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags | extra,
                environment.empty() ? nullptr : environment.data(), directory.empty() ? nullptr : directory.c_str(),
                &startup, &info);
    };
    // Leaving this process's job keeps the program alive when the job ends (a debugger, a terminal). A job that does
    // not allow it refuses with ERROR_ACCESS_DENIED; the program then starts inside it.
    BOOL created = create(CREATE_BREAKAWAY_FROM_JOB);
    if (!created && GetLastError() == ERROR_ACCESS_DENIED) {
        created = create(0);
    }
    if (!created) {
        return std::unexpected(error_from(GetLastError()));
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return {};
}

}

#endif
