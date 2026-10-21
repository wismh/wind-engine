#include "native_build.h"

#include <cctype>
#include <string_view>
#include <utility>

namespace editor {
namespace {

std::string_view trim_right(std::string_view text) {
    if (!text.empty() && text.back() == '\r') {
        text.remove_suffix(1);
    }
    return text;
}

}

std::vector<engine::ProcessVariable> parse_environment_dump(std::string_view text) {
    std::vector<engine::ProcessVariable> variables;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trim_right(end == std::string_view::npos ? text : text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '=') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0) {
            continue;
        }
        variables.push_back(engine::ProcessVariable{
                std::string(line.substr(0, equals)), std::string(line.substr(equals + 1))});
    }
    return variables;
}

#if !defined(_WIN32)

const NativeBuild& native_build() {
    static const NativeBuild ready;
    return ready;
}

#endif

}

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <expected>
#include <filesystem>

namespace editor {
namespace {

constexpr const char* kNoCompiler =
        "Visual Studio with the C++ workload was not found. Wind builds with the MSVC compiler.";
constexpr const char* kNoNinja =
        "Ninja was not found. Install Ninja and put it on PATH, or install the Visual Studio C++ CMake tools.";
constexpr const char* kNoEnvironment = "Visual Studio's developer environment could not be loaded.";

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle)
        : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept
        : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    ~Handle() {
        reset();
    }
    void reset() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
        value = nullptr;
    }
};

std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
            nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring wide_from_acp(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(chars), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

std::wstring search_program(const wchar_t* name) {
    std::wstring buffer(32768, L'\0');
    const DWORD found = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (found == 0 || found >= buffer.size()) {
        return {};
    }
    buffer.resize(found);
    return buffer;
}

bool same_name(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

void prepend_path(std::vector<engine::ProcessVariable>& variables, const std::filesystem::path& directory) {
    const std::string prefix = utf8_from_wide(directory.wstring());
    for (engine::ProcessVariable& variable : variables) {
        if (same_name(variable.name, "PATH")) {
            variable.value = prefix + ";" + variable.value;
            return;
        }
    }
    variables.push_back(engine::ProcessVariable{"PATH", prefix});
}

// Reads the child's stdout until it exits, or until two minutes pass. The caller interprets the bytes.
std::expected<std::string, std::string> capture_bytes(const std::wstring& command, const char* failure) {
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE};
    HANDLE read_raw = nullptr;
    HANDLE write_raw = nullptr;
    if (!CreatePipe(&read_raw, &write_raw, &inherit, 0)) {
        return std::unexpected(failure);
    }
    Handle read{read_raw};
    Handle write{write_raw};
    SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0);
    Handle nul{CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
            nullptr)};
    if (nul.value == INVALID_HANDLE_VALUE) {
        nul.value = nullptr;
        return std::unexpected(failure);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = write.value;
    startup.hStdError = nul.value;
    std::wstring mutable_command = command;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &info);
    write.reset();
    if (!created) {
        return std::unexpected(failure);
    }
    Handle process{info.hProcess};
    Handle thread{info.hThread};

    std::string bytes;
    const ULONGLONG deadline = GetTickCount64() + 120000;
    std::byte buffer[4096];
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read_count = 0;
            if (!ReadFile(read.value, buffer, sizeof(buffer), &read_count, nullptr) || read_count == 0) {
                break;
            }
            bytes.append(reinterpret_cast<const char*>(buffer), read_count);
            continue;
        }
        if (WaitForSingleObject(process.value, 50) == WAIT_OBJECT_0) {
            DWORD read_count = 0;
            while (ReadFile(read.value, buffer, sizeof(buffer), &read_count, nullptr) && read_count > 0) {
                bytes.append(reinterpret_cast<const char*>(buffer), read_count);
            }
            break;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(process.value, 1);
            return std::unexpected(failure);
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.value, &code);
    if (code != 0) {
        return std::unexpected(failure);
    }
    return bytes;
}

// `cmd /u` writes the internal `set` as UTF-16LE. VsDevCmd's own text is sent to nul so it does not mix in.
std::expected<std::string, std::string> capture_unicode_set(const std::wstring& command) {
    const auto bytes = capture_bytes(command, kNoEnvironment);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::string text = *bytes;
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xFF &&
            static_cast<unsigned char>(text[1]) == 0xFE) {
        text.erase(0, 2);
    }
    if (text.size() % 2 != 0) {
        return std::unexpected(kNoEnvironment);
    }
    const auto* wide = reinterpret_cast<const wchar_t*>(text.data());
    return utf8_from_wide(std::wstring_view(wide, text.size() / sizeof(wchar_t)));
}

std::expected<std::filesystem::path, std::string> visual_studio_install() {
    std::wstring vswhere = search_program(L"vswhere.exe");
    if (vswhere.empty()) {
        wchar_t program_files[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            const std::filesystem::path candidate = std::filesystem::path(program_files) / "Microsoft Visual Studio" /
                    "Installer" / "vswhere.exe";
            if (std::filesystem::is_regular_file(candidate)) {
                vswhere = candidate.wstring();
            }
        }
    }
    if (vswhere.empty()) {
        return std::unexpected(kNoCompiler);
    }
    const std::wstring command = L"\"" + vswhere +
            L"\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
            L"-property installationPath";
    const auto output = capture_bytes(command, kNoCompiler);
    if (!output) {
        return std::unexpected(output.error());
    }
    // vswhere writes the active code page. The install path is ASCII.
    std::string_view text = *output;
    const std::size_t line_end = text.find_first_of("\r\n");
    if (line_end != std::string_view::npos) {
        text = text.substr(0, line_end);
    }
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::unexpected(kNoCompiler);
    }
    return std::filesystem::path(wide_from_acp(text));
}

bool has_variable(const std::vector<engine::ProcessVariable>& variables, std::string_view name) {
    for (const engine::ProcessVariable& variable : variables) {
        if (same_name(variable.name, name) && !variable.value.empty()) {
            return true;
        }
    }
    return false;
}

NativeBuild discover() {
    const bool compiler_on_path = !search_program(L"cl.exe").empty();
    const bool ninja_on_path = !search_program(L"ninja.exe").empty();
    if (compiler_on_path && ninja_on_path) {
        return {};
    }
    const auto install = visual_studio_install();
    if (!install) {
        return NativeBuild{.problem = install.error()};
    }
    std::filesystem::path ninja;
    if (!ninja_on_path) {
        ninja = *install / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe";
        if (!std::filesystem::is_regular_file(ninja)) {
            return NativeBuild{.problem = kNoNinja};
        }
    }
    if (!compiler_on_path) {
        const std::wstring vsdev = (*install / "Common7/Tools/VsDevCmd.bat").wstring();
        // `/c call "VsDevCmd.bat"` keeps the path quoted. A nested `call ""path""` is not a command.
        // `/u` makes the internal `set` write UTF-16LE. VsDevCmd's own text goes to nul.
        const std::wstring command = L"cmd.exe /d /u /c call \"" + vsdev +
                L"\" -no_logo -arch=x64 -host_arch=x64 >nul 2>&1 && set";
        const auto output = capture_unicode_set(command);
        if (!output) {
            return NativeBuild{.problem = output.error()};
        }
        NativeBuild build{.environment = parse_environment_dump(*output)};
        if (!has_variable(build.environment, "VCToolsInstallDir")) {
            return NativeBuild{.problem = kNoEnvironment};
        }
        if (!ninja_on_path) {
            prepend_path(build.environment, ninja.parent_path());
        }
        return build;
    }
    // cl is already on PATH. Adding Ninja must keep that PATH: a replacement would drop the compiler.
    const DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring path(needed == 0 ? 0 : needed, L'\0');
    if (needed > 1) {
        GetEnvironmentVariableW(L"PATH", path.data(), needed);
        if (!path.empty() && path.back() == L'\0') {
            path.pop_back();
        }
    }
    NativeBuild build;
    build.environment.push_back(engine::ProcessVariable{
            "PATH", utf8_from_wide(ninja.parent_path().wstring()) + ";" + utf8_from_wide(path)});
    return build;
}

}

const NativeBuild& native_build() {
    static const NativeBuild cached = discover();
    return cached;
}

}

#endif
