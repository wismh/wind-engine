#if defined(_WIN32)
#define _CRT_RAND_S
#define _CRT_SECURE_NO_WARNINGS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "cli/cli_server.h"

#include <engine/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(ENGINE_CLI_SERVER)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_RAND_S
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sddl.h>
#include <cstdlib>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace engine::cli {
    namespace {

        constexpr int kResponseTimeoutMs = 5000;
        constexpr int kSelectTimeoutUs = 200000;

#if defined(_WIN32)
        using Socket = SOCKET;
        constexpr Socket kInvalidSocket = INVALID_SOCKET;
        constexpr int kSendFlags = 0;
#else
        using Socket = int;
        constexpr Socket kInvalidSocket = -1;
        constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

        void close_socket(Socket socket) {
            if (socket == kInvalidSocket) {
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

        std::string base64_encode(const unsigned char *data, std::size_t length) {
            static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve((length + 2) / 3 * 4);
            for (std::size_t i = 0; i < length; i += 3) {
                const unsigned b0 = data[i];
                const unsigned b1 = i + 1 < length ? data[i + 1] : 0;
                const unsigned b2 = i + 2 < length ? data[i + 2] : 0;
                const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
                out += kTable[(triple >> 18) & 63u];
                out += kTable[(triple >> 12) & 63u];
                out += i + 1 < length ? kTable[(triple >> 6) & 63u] : '=';
                out += i + 2 < length ? kTable[triple & 63u] : '=';
            }
            return out;
        }

        std::string random_token() {
            std::array<unsigned char, 32> bytes{};
#if defined(_WIN32)
            for (std::size_t i = 0; i < bytes.size();) {
                unsigned int value = 0;
                if (rand_s(&value) != 0) {
                    return {};
                }
                for (int shift = 0; shift < 4 && i < bytes.size(); ++shift, ++i) {
                    bytes[static_cast<std::size_t>(i)] = static_cast<unsigned char>(value & 0xffu);
                    value >>= 8;
                }
            }
#else
            FILE *file = std::fopen("/dev/urandom", "rb");
            if (file == nullptr) {
                return {};
            }
            const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
            std::fclose(file);
            if (read != bytes.size()) {
                return {};
            }
#endif
            return base64_encode(bytes.data(), bytes.size());
        }

        bool token_equal(std::string_view a, std::string_view b) {
            const std::size_t n = std::max(a.size(), b.size());
            unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
            for (std::size_t i = 0; i < n; ++i) {
                const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
                const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
                diff |= static_cast<unsigned>(ca ^ cb);
            }
            return diff == 0;
        }

        std::string executable_path() {
#if defined(_WIN32)
            std::wstring wide(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, wide.data(), static_cast<DWORD>(wide.size()));
            if (length == 0) {
                return {};
            }
            wide.resize(length);
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                                   nullptr, nullptr);
            if (bytes <= 0) {
                return {};
            }
            std::string utf8(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), bytes, nullptr,
                                nullptr);
            return utf8;
#elif defined(__APPLE__)
            char buffer[4096];
            std::uint32_t size = sizeof(buffer);
            if (_NSGetExecutablePath(buffer, &size) != 0) {
                return {};
            }
            return std::string(buffer);
#else
            char buffer[4096];
            const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (length <= 0) {
                return {};
            }
            buffer[length] = '\0';
            return std::string(buffer);
#endif
        }

        std::uint32_t process_id() {
#if defined(_WIN32)
            return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
            return static_cast<std::uint32_t>(::getpid());
#endif
        }

        bool write_private(const std::filesystem::path &path, std::string_view bytes) {
#if defined(_WIN32)
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor,
                                                                      nullptr)) {
                return false;
            }
            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_ALWAYS,
                                             FILE_ATTRIBUTE_NORMAL, nullptr);
            LocalFree(descriptor);
            if (file == INVALID_HANDLE_VALUE) {
                return false;
            }
            DWORD written = 0;
            const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
            return ok && written == bytes.size();
#else
            const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                return false;
            }
            std::size_t offset = 0;
            while (offset < bytes.size()) {
                const ssize_t wrote = ::write(fd, bytes.data() + offset, bytes.size() - offset);
                if (wrote <= 0) {
                    ::close(fd);
                    return false;
                }
                offset += static_cast<std::size_t>(wrote);
            }
            ::close(fd);
            return true;
#endif
        }

        bool ensure_directory(const std::filesystem::path &directory) {
#if defined(_WIN32)
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            return std::filesystem::is_directory(directory);
#else
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (!std::filesystem::is_directory(directory)) {
                return false;
            }
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all, error);
            return true;
#endif
        }

        enum class Phase { New, ClickArmed, ClickDone, ProfileWait };

        struct Job {
            std::mutex mutex;
            std::condition_variable cv;
            std::string body;
            std::string response;
            int http_status = 200;
            bool done = false;
            bool abandoned = false;
            Phase phase = Phase::New;
        };

        struct Server {
            std::mutex mutex;
            std::condition_variable queued;
            std::vector<std::shared_ptr<Job>> jobs;
            std::thread thread;
            std::atomic<bool> running{false};
            Socket listen = kInvalidSocket;
            std::string token;
            std::filesystem::path descriptor;
            bool wsa = false;
        };

        Server &server() {
            static Server instance;
            return instance;
        }

        void finish(Job &job, int status, std::string body) {
            std::lock_guard lock(job.mutex);
            if (job.done) {
                return;
            }
            job.http_status = status;
            job.response = std::move(body);
            job.done = true;
            job.cv.notify_one();
        }

        bool job_done(Job &job) {
            std::lock_guard lock(job.mutex);
            return job.done;
        }

        void respond(Socket socket, int status, std::string_view reason, std::string_view body) {
            const std::string headers = std::format("HTTP/1.1 {} {}\r\nContent-Type: application/json\r\n"
                                                     "Content-Length: {}\r\nConnection: close\r\n\r\n",
                                                     status, reason, body.size());
            send_all(socket, headers);
            send_all(socket, body);
        }

        std::string header_value(std::string_view headers, std::string_view name) {
            std::size_t line = 0;
            while (line < headers.size()) {
                const std::size_t end = headers.find("\r\n", line);
                const std::string_view row = headers.substr(line, end == std::string_view::npos ? headers.size() - line
                                                                                                 : end - line);
                const std::size_t colon = row.find(':');
                if (colon != std::string_view::npos) {
                    std::string_view key = row.substr(0, colon);
                    if (key.size() == name.size()) {
                        bool match = true;
                        for (std::size_t i = 0; i < key.size(); ++i) {
                            char a = key[i];
                            if (a >= 'A' && a <= 'Z') {
                                a = static_cast<char>(a - 'A' + 'a');
                            }
                            const char b = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a')
                                                                             : name[i];
                            if (a != b) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            std::string_view value = row.substr(colon + 1);
                            while (!value.empty() && value.front() == ' ') {
                                value.remove_prefix(1);
                            }
                            return std::string(value);
                        }
                    }
                }
                if (end == std::string_view::npos) {
                    break;
                }
                line = end + 2;
            }
            return {};
        }

        bool header_present(std::string_view headers, std::string_view name) {
            std::size_t line = 0;
            while (line < headers.size()) {
                const std::size_t end = headers.find("\r\n", line);
                const std::string_view row = headers.substr(line, end == std::string_view::npos ? headers.size() - line
                                                                                                 : end - line);
                const std::size_t colon = row.find(':');
                if (colon != std::string_view::npos && colon == name.size()) {
                    bool match = true;
                    for (std::size_t i = 0; i < name.size(); ++i) {
                        const char a = row[i] >= 'A' && row[i] <= 'Z' ? static_cast<char>(row[i] - 'A' + 'a') : row[i];
                        const char b = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a')
                                                                         : name[i];
                        if (a != b) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        return true;
                    }
                }
                if (end == std::string_view::npos) {
                    break;
                }
                line = end + 2;
            }
            return false;
        }

        void handle_client(Socket client) {
            std::string data;
            char buffer[2048];
            while (data.find("\r\n\r\n") == std::string::npos) {
                const int got = ::recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0 || data.size() > 16384) {
                    close_socket(client);
                    return;
                }
                data.append(buffer, static_cast<std::size_t>(got));
            }
            const std::size_t split = data.find("\r\n\r\n");
            const std::string headers = data.substr(0, split);
            std::string body = data.substr(split + 4);
            const std::string length_text = header_value(headers, "content-length");
            std::size_t length = 0;
            if (length_text.empty()) {
                respond(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid request\"}");
                close_socket(client);
                return;
            }
            const auto [ptr, ec] = std::from_chars(length_text.data(), length_text.data() + length_text.size(), length);
            if (ec != std::errc{} || ptr != length_text.data() + length_text.size() || length > (1u << 20)) {
                respond(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid request\"}");
                close_socket(client);
                return;
            }
            while (body.size() < length) {
                const int got = ::recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0) {
                    close_socket(client);
                    return;
                }
                body.append(buffer, static_cast<std::size_t>(got));
            }
            body.resize(length);

            const std::string request_line = headers.substr(0, headers.find("\r\n"));
            if (request_line.rfind("POST /exec ", 0) != 0) {
                respond(client, 404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}");
                close_socket(client);
                return;
            }
            if (header_present(headers, "origin")) {
                respond(client, 403, "Forbidden", "{\"ok\":false,\"error\":\"origin\"}");
                close_socket(client);
                return;
            }
            const std::string authorization = header_value(headers, "authorization");
            constexpr std::string_view kBearer = "Bearer ";
            std::string_view presented = authorization;
            if (presented.starts_with(kBearer)) {
                presented.remove_prefix(kBearer.size());
            } else {
                presented = {};
            }
            if (!token_equal(presented, server().token)) {
                respond(client, 401, "Unauthorized", "{\"ok\":false,\"error\":\"unauthorized\"}");
                close_socket(client);
                return;
            }

            auto job = std::make_shared<Job>();
            job->body = std::move(body);
            {
                std::lock_guard lock(server().mutex);
                server().jobs.push_back(job);
            }
            server().queued.notify_all();

            {
                std::unique_lock lock(job->mutex);
                job->cv.wait_for(lock, std::chrono::milliseconds(kResponseTimeoutMs), [&] {
                    return job->done || !server().running.load();
                });
                if (!job->done) {
                    job->done = true;
                    job->abandoned = true;
                }
                const bool abandoned = job->abandoned;
                const int status = job->http_status;
                const std::string response = job->response;
                const bool stopped = !server().running.load();
                lock.unlock();
                if (abandoned && stopped) {
                    respond(client, 503, "Service Unavailable", "{\"ok\":false,\"error\":\"stopped\"}");
                } else if (abandoned) {
                    respond(client, 504, "Gateway Timeout", "{\"ok\":false,\"error\":\"timeout\"}");
                } else {
                    const char *reason = status == 200 ? "OK" : "Error";
                    respond(client, status, reason, response);
                }
            }
            close_socket(client);
        }

        void accept_loop() {
            while (server().running.load()) {
                fd_set read_set;
                FD_ZERO(&read_set);
                FD_SET(server().listen, &read_set);
                timeval timeout{};
                timeout.tv_sec = 0;
                timeout.tv_usec = kSelectTimeoutUs;
#if defined(_WIN32)
                const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
                const int ready = ::select(server().listen + 1, &read_set, nullptr, nullptr, &timeout);
#endif
                if (!server().running.load() || ready <= 0) {
                    continue;
                }
                const Socket client = ::accept(server().listen, nullptr, nullptr);
                if (client == kInvalidSocket) {
                    continue;
                }
                handle_client(client);
            }
        }

        void dispatch(ecs::World &world, Job &job, bool begin) {
            if (job_done(job)) {
                return;
            }
            const CliRequest request = parse_request(job.body);
            if (!request.error.empty()) {
                finish(job, 200, execute(world, request).json);
                return;
            }
            if (begin) {
                if (job.phase != Phase::ClickArmed) {
                    return;
                }
                const CliResponse response = execute(world, request);
                {
                    std::lock_guard lock(job.mutex);
                    if (job.done) {
                        return;
                    }
                    job.response = response.json;
                    job.phase = Phase::ClickDone;
                }
                return;
            }
            if (job.phase == Phase::ClickDone) {
                std::string body;
                {
                    std::lock_guard lock(job.mutex);
                    body = job.response;
                }
                finish(job, 200, std::move(body));
                return;
            }
            if (job.phase == Phase::ClickArmed) {
                return;
            }
            if (request.command == "click" && job.phase == Phase::New) {
                job.phase = Phase::ClickArmed;
                return;
            }
            if (request.command == "profile" && !request.stop) {
                const CliResponse response = execute(world, request);
                if (response.pending) {
                    job.phase = Phase::ProfileWait;
                    return;
                }
                finish(job, 200, response.json);
                return;
            }
            finish(job, 200, execute(world, request).json);
        }

    } // namespace

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

    void start() {
        Server &self = server();
        if (self.running.load()) {
            return;
        }
        const std::filesystem::path directory = descriptor_directory();
        if (directory.empty() || !ensure_directory(directory)) {
            log::error("wind-cli server: descriptor directory is not available");
            return;
        }
        const std::string token = random_token();
        if (token.empty()) {
            log::error("wind-cli server: could not generate a token");
            return;
        }

#if defined(_WIN32)
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            log::error("wind-cli server: WSAStartup failed");
            return;
        }
        self.wsa = true;
#endif
        const Socket listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen == kInvalidSocket) {
            log::error("wind-cli server: socket failed");
#if defined(_WIN32)
            WSACleanup();
            self.wsa = false;
#endif
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listen, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || ::listen(listen, 4) != 0) {
            close_socket(listen);
            log::error("wind-cli server: bind failed");
#if defined(_WIN32)
            WSACleanup();
            self.wsa = false;
#endif
            return;
        }
        sockaddr_in bound{};
#if defined(_WIN32)
        int bound_size = sizeof(bound);
#else
        socklen_t bound_size = sizeof(bound);
#endif
        if (::getsockname(listen, reinterpret_cast<sockaddr *>(&bound), &bound_size) != 0) {
            close_socket(listen);
            log::error("wind-cli server: getsockname failed");
#if defined(_WIN32)
            WSACleanup();
            self.wsa = false;
#endif
            return;
        }
        const unsigned port = ntohs(bound.sin_port);
        const std::filesystem::path file = directory / (std::to_string(process_id()) + ".json");
        const std::string json = std::format("{{\"pid\":{},\"port\":{},\"token\":\"{}\",\"exe\":\"{}\"}}", process_id(),
                                              port, token, json_escape(executable_path()));
        if (!write_private(file, json)) {
            close_socket(listen);
            log::error("wind-cli server: could not write the descriptor");
#if defined(_WIN32)
            WSACleanup();
            self.wsa = false;
#endif
            return;
        }

        self.token = token;
        self.listen = listen;
        self.descriptor = file;
        self.running.store(true);
        self.thread = std::thread(accept_loop);
    }

    void stop() {
        Server &self = server();
        if (!self.running.load() && self.listen == kInvalidSocket) {
            return;
        }
        self.running.store(false);
        self.queued.notify_all();
        if (self.thread.joinable()) {
            self.thread.join();
        }
        close_socket(self.listen);
        self.listen = kInvalidSocket;
        {
            std::lock_guard lock(self.mutex);
            for (const std::shared_ptr<Job> &job: self.jobs) {
                finish(*job, 503, "{\"ok\":false,\"error\":\"stopped\"}");
            }
            self.jobs.clear();
        }
        std::error_code error;
        std::filesystem::remove(self.descriptor, error);
        self.descriptor.clear();
        self.token.clear();
#if defined(_WIN32)
        if (self.wsa) {
            WSACleanup();
            self.wsa = false;
        }
#endif
    }

    void begin_frame(ecs::World &world) {
        Server &self = server();
        if (!self.running.load()) {
            return;
        }
        std::vector<std::shared_ptr<Job>> jobs;
        {
            std::lock_guard lock(self.mutex);
            jobs = self.jobs;
        }
        for (const std::shared_ptr<Job> &job: jobs) {
            dispatch(world, *job, true);
        }
    }

    void drain(ecs::World &world) {
        Server &self = server();
        if (!self.running.load()) {
            return;
        }
        std::vector<std::shared_ptr<Job>> jobs;
        {
            std::lock_guard lock(self.mutex);
            jobs = self.jobs;
        }
        for (const std::shared_ptr<Job> &job: jobs) {
            dispatch(world, *job, false);
        }
        std::lock_guard lock(self.mutex);
        self.jobs.erase(std::remove_if(self.jobs.begin(), self.jobs.end(),
                                        [](const std::shared_ptr<Job> &job) { return job_done(*job); }),
                        self.jobs.end());
    }

    void wait_for_request() {
        Server &self = server();
        std::unique_lock lock(self.mutex);
        self.queued.wait_for(lock, std::chrono::seconds(2), [&] { return !self.jobs.empty() || !self.running.load(); });
    }

} // namespace engine::cli

#endif
