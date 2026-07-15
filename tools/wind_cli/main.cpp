#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
constexpr int kSendFlags = 0;
#else
using Socket = int;
constexpr Socket kInvalid = -1;
constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

void close_socket(Socket socket) {
    if (socket == kInvalid) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    ::close(socket);
#endif
}

bool send_all(Socket socket, std::string_view data) {
    while (!data.empty()) {
        const int sent = ::send(socket, data.data(), static_cast<int>(data.size()), kSendFlags);
        if (sent <= 0) {
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

std::filesystem::path descriptor_directory() {
#if defined(_WIN32)
    const char *local = std::getenv("LOCALAPPDATA");
    if (local == nullptr || local[0] == '\0') {
        return {};
    }
    return std::filesystem::path(local) / "wind" / "cli";
#else
    if (const char *runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && runtime[0] != '\0') {
        return std::filesystem::path(runtime) / "wind" / "cli";
    }
    return std::filesystem::path("/tmp") / ("wind-cli-" + std::to_string(static_cast<unsigned>(::getuid())));
#endif
}

bool process_alive(std::uint32_t pid) {
#if defined(_WIN32)
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

std::string json_escape(std::string_view text) {
    std::string out;
    for (const unsigned char c: text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                if (c < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

void skip_ws(std::string_view &in) {
    while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\n' || in.front() == '\r')) {
        in.remove_prefix(1);
    }
}

bool parse_string(std::string_view &in, std::string &out) {
    if (in.empty() || in.front() != '"') {
        return false;
    }
    in.remove_prefix(1);
    out.clear();
    while (!in.empty()) {
        const char c = in.front();
        in.remove_prefix(1);
        if (c == '"') {
            return true;
        }
        if (c != '\\') {
            out += c;
            continue;
        }
        if (in.empty()) {
            return false;
        }
        const char esc = in.front();
        in.remove_prefix(1);
        if (esc == '"' || esc == '\\' || esc == '/') {
            out += esc;
        } else if (esc == 'n') {
            out += '\n';
        } else if (esc == 'r') {
            out += '\r';
        } else if (esc == 't') {
            out += '\t';
        } else if (esc == 'b') {
            out += '\b';
        } else if (esc == 'f') {
            out += '\f';
        } else if (esc == 'u' && in.size() >= 4) {
            unsigned code = 0;
            const auto [ptr, ec] = std::from_chars(in.data(), in.data() + 4, code, 16);
            if (ec != std::errc{} || ptr != in.data() + 4) {
                return false;
            }
            in.remove_prefix(4);
            // The server escapes only control characters this way; other text arrives as UTF-8.
            out += code < 0x80 ? static_cast<char>(code) : '?';
        } else {
            return false;
        }
    }
    return false;
}

std::optional<std::string> json_string(std::string_view json, std::string_view key) {
    const std::string needle = std::string("\"") + std::string(key) + "\"";
    const std::size_t at = json.find(needle);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = json.substr(at + needle.size());
    skip_ws(rest);
    if (rest.empty() || rest.front() != ':') {
        return std::nullopt;
    }
    rest.remove_prefix(1);
    skip_ws(rest);
    std::string value;
    if (!parse_string(rest, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> json_int(std::string_view json, std::string_view key) {
    const std::string needle = std::string("\"") + std::string(key) + "\"";
    const std::size_t at = json.find(needle);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = json.substr(at + needle.size());
    skip_ws(rest);
    if (rest.empty() || rest.front() != ':') {
        return std::nullopt;
    }
    rest.remove_prefix(1);
    skip_ws(rest);
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

struct Instance {
    std::uint32_t pid = 0;
    int port = 0;
    std::string token;
    std::string exe;
    // "editor" or "game". A descriptor written before the field existed is a game.
    std::string kind;
};

std::vector<Instance> live_instances() {
    std::vector<Instance> found;
    const std::filesystem::path directory = descriptor_directory();
    if (directory.empty() || !std::filesystem::is_directory(directory)) {
        return found;
    }
    for (const std::filesystem::directory_entry &entry: std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream file(entry.path());
        const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::optional<std::int64_t> pid = json_int(json, "pid");
        const std::optional<std::int64_t> port = json_int(json, "port");
        const std::optional<std::string> token = json_string(json, "token");
        const std::optional<std::string> exe = json_string(json, "exe");
        const std::optional<std::string> kind = json_string(json, "kind");
        if (!pid || !port || !token || *pid < 0 || *port <= 0 || *port > 65535) {
            continue;
        }
        if (!process_alive(static_cast<std::uint32_t>(*pid))) {
            std::error_code error;
            std::filesystem::remove(entry.path(), error);
            continue;
        }
        found.push_back(Instance{static_cast<std::uint32_t>(*pid), static_cast<int>(*port), *token,
                                 exe ? *exe : std::string{}, kind ? *kind : std::string("game")});
    }
    return found;
}

// `editor`: only instances of kind "editor" count, so a standalone game beside the editor does not need --pid.
std::optional<Instance> pick_instance(const std::vector<Instance> &all, bool has_pid, std::uint32_t pid, bool editor) {
    std::vector<Instance> instances;
    for (const Instance &instance: all) {
        if (!editor || instance.kind == "editor") {
            instances.push_back(instance);
        }
    }
    // A UI command reaches a game or an editor that plays one.
    const char *what = editor ? "editor" : "game or editor";
    if (has_pid) {
        for (const Instance &instance: instances) {
            if (instance.pid == pid) {
                return instance;
            }
        }
        std::cerr << "no " << what << " with pid " << pid << "\n";
        return std::nullopt;
    }
    if (instances.empty()) {
        std::cerr << "no " << what << " is listening\n";
        return std::nullopt;
    }
    if (instances.size() > 1) {
        std::cerr << "more than one " << what << " is listening; pass --pid\n";
        for (const Instance &instance: instances) {
            std::cerr << instance.pid << " " << instance.kind << " " << instance.exe << "\n";
        }
        return std::nullopt;
    }
    return instances.front();
}

std::string status_json(const std::vector<Instance> &instances) {
    std::string out = "{\"ok\":true,\"result\":[";
    bool first = true;
    for (const Instance &instance: instances) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += std::format("{{\"pid\":{},\"port\":{},\"kind\":\"{}\",\"exe\":\"{}\"}}", instance.pid, instance.port,
                           json_escape(instance.kind), json_escape(instance.exe));
    }
    out += "]}";
    return out;
}

bool http_exchange(const Instance &instance, std::string_view body, std::string &response) {
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
    }
#endif
    const Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalid) {
        std::cerr << "socket failed\n";
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }
#if defined(_WIN32)
    const DWORD timeout = 8000;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = 8;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(instance.port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        std::cerr << "connect failed\n";
        close_socket(socket);
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }
    const std::string request =
            std::format("POST /exec HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                        "Content-Length: {}\r\nAuthorization: Bearer {}\r\nConnection: close\r\n\r\n{}",
                        body.size(), instance.token, body);
    if (!send_all(socket, request)) {
        std::cerr << "send failed\n";
        close_socket(socket);
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }
    std::string data;
    char buffer[2048];
    while (true) {
        const int got = ::recv(socket, buffer, sizeof(buffer), 0);
        if (got < 0) {
            std::cerr << "recv failed\n";
            close_socket(socket);
#if defined(_WIN32)
            WSACleanup();
#endif
            return false;
        }
        if (got == 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(got));
    }
    close_socket(socket);
#if defined(_WIN32)
    WSACleanup();
#endif
    const std::size_t split = data.find("\r\n\r\n");
    if (split == std::string::npos) {
        std::cerr << "bad response\n";
        return false;
    }
    response = data.substr(split + 4);
    return true;
}

std::string utf8(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

// The `result` value of a reply `{"ok":...,"result":X}`, which the server always writes last; "null" without one.
std::string result_of(std::string_view body) {
    const std::size_t at = body.find("\"result\":");
    if (at == std::string_view::npos || body.empty() || body.back() != '}') {
        return "null";
    }
    const std::size_t begin = at + std::string_view("\"result\":").size();
    return std::string(body.substr(begin, body.size() - begin - 1));
}

// The directory wind-cli runs from. In an SDK that is `<sdk>/bin`, beside `wind_editor`.
std::filesystem::path own_directory() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#else
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
#endif
}

std::filesystem::path default_editor() {
#if defined(_WIN32)
    return own_directory() / "wind_editor.exe";
#else
    return own_directory() / "wind_editor";
#endif
}

// `argument` is a project directory or its wind_project.toml, relative to the current directory or absolute.
std::optional<std::filesystem::path> project_directory(std::string_view argument) {
    std::error_code error;
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::path(argument), error);
    if (error) {
        std::cerr << "bad project path " << argument << "\n";
        return std::nullopt;
    }
    if (directory.filename() == "wind_project.toml") {
        directory = directory.parent_path();
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
    if (!error) {
        directory = canonical;
    }
    if (!std::filesystem::is_regular_file(directory / "wind_project.toml", error)) {
        std::cerr << "no wind_project.toml in " << utf8(directory) << "\n";
        return std::nullopt;
    }
    return directory;
}

#if defined(_WIN32)
// One argv entry for the child's CommandLineToArgvW rules.
std::wstring quote_argument(const std::wstring &argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c: argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        out += c;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}
#endif

// Starts the editor on its own, the way the launcher does: detached, in the editor's directory. Its pid, or
// nullopt after printing why.
std::optional<std::uint32_t> start_editor(const std::filesystem::path &editor, const std::filesystem::path &project,
                                          bool play) {
#if defined(_WIN32)
    std::wstring command = L"\"" + editor.wstring() + L"\" --project " + quote_argument(project.wstring());
    if (play) {
        command += L" --play";
    }
    const std::wstring directory = editor.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    const auto create = [&](DWORD extra) {
        return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags | extra, nullptr,
                              directory.c_str(), &startup, &info);
    };
    // Leave this terminal's job so the editor outlives it; a job that does not allow that refuses with
    // ERROR_ACCESS_DENIED, and the editor then starts inside it.
    BOOL created = create(CREATE_BREAKAWAY_FROM_JOB);
    if (!created && GetLastError() == ERROR_ACCESS_DENIED) {
        created = create(0);
    }
    if (!created) {
        std::cerr << "could not start " << utf8(editor) << " (error " << GetLastError() << ")\n";
        return std::nullopt;
    }
    const std::uint32_t pid = info.dwProcessId;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return pid;
#else
    (void) editor;
    (void) project;
    (void) play;
    std::cerr << "launch is not supported on this platform\n";
    return std::nullopt;
#endif
}

// The started editor's descriptor, once its loop runs. Nullopt when it exits first or the timeout passes.
std::optional<Instance> wait_for_descriptor(std::uint32_t pid, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const Instance &instance: live_instances()) {
            if (instance.pid == pid) {
                return instance;
            }
        }
        if (!process_alive(pid)) {
            std::cerr << "the editor exited before it listened (pid " << pid << "); see its game.log\n";
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "the editor did not listen within " << timeout.count() << " s (pid " << pid << ")\n";
    return std::nullopt;
}

// Polls `state` until the editor plays (true) or is idle again (false: the build or Play failed, or the game quit
// already). `last` is the last state reply.
bool wait_for_play(const Instance &instance, std::chrono::seconds timeout, std::string &last) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (!http_exchange(instance, "{\"command\":\"state\"}", last)) {
            return false;
        }
        const std::optional<std::string> run = json_string(last, "run");
        if (!run) {
            return false;
        }
        if (*run == "playing") {
            return true;
        }
        if (*run == "idle") {
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "still " << *run << " after " << timeout.count() << " s\n";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void usage() {
    std::cerr << "usage: wind-cli status\n"
                 "       wind-cli launch <project> [--play [--wait [S]]] [--editor PATH]\n"
                 "       wind-cli state [--pid N]\n"
                 "       wind-cli play [--wait [S]] [--pid N]\n"
                 "       wind-cli stop [--pid N]\n"
                 "       wind-cli open <project> [--pid N]\n"
                 "       wind-cli tree [--window N] [--pid N]\n"
                 "       wind-cli element <selector> [--window N] [--pid N]\n"
                 "       wind-cli hit <x> <y> [--window N] [--pid N]\n"
                 "       wind-cli click <selector> [--window N] [--pid N]\n"
                 "       wind-cli screenshot [selector] [--out FILE] [--window N] [--pid N]\n"
                 "       wind-cli profile [stop] [--pid N]\n";
}

// screenshot-YYYYMMDD-HHMMSS.png, local time.
std::string default_screenshot_name() {
    const std::time_t now = std::time(nullptr);
    char stamp[32] = {};
    if (const std::tm *local = std::localtime(&now)) {
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
    }
    return std::format("screenshot-{}.png", stamp);
}

constexpr std::chrono::seconds kListenTimeout{30};
// A first build of a game can take minutes.
constexpr std::chrono::seconds kDefaultWait{600};

} // namespace

int main(int argc, char **argv) {
    bool has_pid = false;
    std::uint32_t pid = 0;
    bool has_window = false;
    std::uint32_t window = 0;
    bool stop = false;
    bool play = false;
    bool wait = false;
    std::uint32_t wait_seconds = static_cast<std::uint32_t>(kDefaultWait.count());
    std::optional<std::filesystem::path> editor;
    std::optional<std::filesystem::path> out_file;
    std::vector<std::string> positionals;
    const auto parse_uint = [](std::string_view text, std::uint32_t &out) {
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc{} && ptr == text.data() + text.size();
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto take_uint = [&](std::uint32_t &out) {
            if (i + 1 >= argc) {
                return false;
            }
            ++i;
            return parse_uint(argv[i], out);
        };
        if (arg == "--pid") {
            if (!take_uint(pid)) {
                usage();
                return 2;
            }
            has_pid = true;
        } else if (arg == "--window") {
            if (!take_uint(window)) {
                usage();
                return 2;
            }
            has_window = true;
        } else if (arg == "--stop") {
            stop = true;
        } else if (arg == "--play") {
            play = true;
        } else if (arg == "--wait") {
            wait = true;
            std::uint32_t seconds = 0;
            if (i + 1 < argc && parse_uint(argv[i + 1], seconds)) {
                wait_seconds = seconds;
                ++i;
            }
        } else if (arg == "--editor") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            ++i;
            editor = std::filesystem::path(argv[i]);
        } else if (arg == "--out") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            ++i;
            out_file = std::filesystem::path(argv[i]);
        } else if (arg.starts_with('-')) {
            usage();
            return 2;
        } else {
            positionals.emplace_back(arg);
        }
    }
    if (positionals.empty()) {
        usage();
        return 2;
    }
    const std::string &command = positionals[0];
    const bool editor_command = command == "state" || command == "play" || command == "stop" || command == "open";
    if ((play && command != "launch") || (editor && command != "launch") || (out_file && command != "screenshot") ||
        (wait && !(command == "play" || (command == "launch" && play)))) {
        usage();
        return 2;
    }

    const std::vector<Instance> instances = live_instances();
    if (command == "status") {
        std::cout << status_json(instances) << "\n";
        return 0;
    }

    if (command == "launch") {
        if (positionals.size() != 2) {
            usage();
            return 2;
        }
        const std::optional<std::filesystem::path> project = project_directory(positionals[1]);
        if (!project) {
            return 1;
        }
        const std::filesystem::path program = editor ? std::filesystem::absolute(*editor) : default_editor();
        if (!std::filesystem::is_regular_file(program)) {
            std::cerr << "no editor at " << utf8(program) << "; pass --editor\n";
            return 1;
        }
        const std::optional<std::uint32_t> started = start_editor(program, *project, play);
        if (!started) {
            return 1;
        }
        const std::optional<Instance> instance = wait_for_descriptor(*started, kListenTimeout);
        if (!instance) {
            return 1;
        }
        const std::string launched = std::format("\"pid\":{},\"port\":{}", instance->pid, instance->port);
        if (!wait) {
            std::cout << "{\"ok\":true,\"result\":{" << launched << "}}\n";
            return 0;
        }
        std::string last;
        const bool playing = wait_for_play(*instance, std::chrono::seconds(wait_seconds), last);
        if (playing) {
            std::cout << "{\"ok\":true,\"result\":{" << launched << ",\"state\":" << result_of(last) << "}}\n";
            return 0;
        }
        const std::string status = json_string(last, "status").value_or("the editor did not play");
        std::cout << "{\"ok\":false,\"error\":\"" << json_escape(status) << "\",\"result\":{" << launched
                  << ",\"state\":" << result_of(last) << "}}\n";
        return 1;
    }

    const std::optional<Instance> instance = pick_instance(instances, has_pid, pid, editor_command);
    if (!instance) {
        return 1;
    }

    std::string body = std::format("{{\"command\":\"{}\"", command);
    if (has_window) {
        body += std::format(",\"window\":{}", window);
    }
    if (command == "element" || command == "click") {
        if (positionals.size() != 2) {
            usage();
            return 2;
        }
        body += ",\"selector\":\"" + json_escape(positionals[1]) + "\"";
    } else if (command == "hit") {
        if (positionals.size() != 3) {
            usage();
            return 2;
        }
        double x = 0.0;
        double y = 0.0;
        const auto parse_double = [](std::string_view text, double &out) {
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
            return ec == std::errc{} && ptr == text.data() + text.size();
        };
        if (!parse_double(positionals[1], x) || !parse_double(positionals[2], y)) {
            usage();
            return 2;
        }
        body += std::format(",\"x\":{:.6g},\"y\":{:.6g}", x, y);
    } else if (command == "profile") {
        if (positionals.size() > 2 || (positionals.size() == 2 && positionals[1] != "stop")) {
            usage();
            return 2;
        }
        if (positionals.size() == 2) {
            stop = true;
        }
        if (stop) {
            body += ",\"stop\":true";
        }
    } else if (command == "open") {
        if (positionals.size() != 2) {
            usage();
            return 2;
        }
        std::error_code error;
        const std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(positionals[1]), error);
        if (error) {
            std::cerr << "bad project path " << positionals[1] << "\n";
            return 1;
        }
        body += ",\"path\":\"" + json_escape(utf8(path)) + "\"";
    } else if (command == "screenshot") {
        if (positionals.size() > 2) {
            usage();
            return 2;
        }
        if (positionals.size() == 2) {
            body += ",\"selector\":\"" + json_escape(positionals[1]) + "\"";
        }
        std::error_code error;
        const std::filesystem::path name = out_file ? *out_file : std::filesystem::path(default_screenshot_name());
        const std::filesystem::path path = std::filesystem::absolute(name, error);
        if (error) {
            std::cerr << "bad output path\n";
            return 1;
        }
        body += ",\"path\":\"" + json_escape(utf8(path)) + "\"";
    } else if (command == "tree" || command == "state" || command == "play" || command == "stop") {
        if (positionals.size() != 1) {
            usage();
            return 2;
        }
    } else {
        usage();
        return 2;
    }
    body += '}';

    std::string response;
    if (!http_exchange(*instance, body, response)) {
        return 1;
    }
    if (command == "play" && wait && response.starts_with("{\"ok\":true")) {
        const bool playing = wait_for_play(*instance, std::chrono::seconds(wait_seconds), response);
        std::cout << response << "\n";
        return playing ? 0 : 1;
    }
    std::cout << response << "\n";
    return response.starts_with("{\"ok\":true") ? 0 : 1;
}
