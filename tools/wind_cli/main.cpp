#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
        } else if (esc == 'u' && in.size() >= 4) {
            in.remove_prefix(4);
            out += '?';
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
        if (!pid || !port || !token || *pid < 0 || *port <= 0 || *port > 65535) {
            continue;
        }
        if (!process_alive(static_cast<std::uint32_t>(*pid))) {
            std::error_code error;
            std::filesystem::remove(entry.path(), error);
            continue;
        }
        found.push_back(Instance{static_cast<std::uint32_t>(*pid), static_cast<int>(*port), *token,
                                 exe ? *exe : std::string{}});
    }
    return found;
}

std::optional<Instance> pick_instance(const std::vector<Instance> &instances, bool has_pid, std::uint32_t pid) {
    if (has_pid) {
        for (const Instance &instance: instances) {
            if (instance.pid == pid) {
                return instance;
            }
        }
        std::cerr << "no game with pid " << pid << "\n";
        return std::nullopt;
    }
    if (instances.empty()) {
        std::cerr << "no game is listening\n";
        return std::nullopt;
    }
    if (instances.size() > 1) {
        std::cerr << "more than one game is listening; pass --pid\n";
        for (const Instance &instance: instances) {
            std::cerr << instance.pid << " " << instance.exe << "\n";
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
        out += std::format("{{\"pid\":{},\"port\":{},\"exe\":\"{}\"}}", instance.pid, instance.port,
                           json_escape(instance.exe));
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

void usage() {
    std::cerr << "usage: wind-cli status\n"
                 "       wind-cli tree [--window N] [--pid N]\n"
                 "       wind-cli element <selector> [--window N] [--pid N]\n"
                 "       wind-cli hit <x> <y> [--window N] [--pid N]\n"
                 "       wind-cli click <selector> [--window N] [--pid N]\n"
                 "       wind-cli profile [stop] [--pid N]\n";
}

} // namespace

int main(int argc, char **argv) {
    bool has_pid = false;
    std::uint32_t pid = 0;
    bool has_window = false;
    std::uint32_t window = 0;
    bool stop = false;
    std::vector<std::string> positionals;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto take_uint = [&](std::uint32_t &out) {
            if (i + 1 >= argc) {
                return false;
            }
            ++i;
            std::uint32_t value = 0;
            const std::string_view text = argv[i];
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc{} || ptr != text.data() + text.size()) {
                return false;
            }
            out = value;
            return true;
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

    const std::vector<Instance> instances = live_instances();
    if (command == "status") {
        std::cout << status_json(instances) << "\n";
        return 0;
    }

    const std::optional<Instance> instance = pick_instance(instances, has_pid, pid);
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
    } else if (command == "tree") {
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
    std::cout << response << "\n";
    return response.starts_with("{\"ok\":true") ? 0 : 1;
}
