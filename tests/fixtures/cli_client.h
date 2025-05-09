#pragma once

// A loopback client for the wind-cli server, the way tools/wind_cli talks to it: read this process's
// descriptor, then POST /exec. Used by tests/cli_server_test.cpp and tests/game_loop_test.cpp.

#if defined(ENGINE_CLI_SERVER)

#include "cli/cli_server.h"

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

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace cli_client {

struct Reply {
    int status = 0;
    std::string body;
};

// This process's descriptor fields. `port` is 0 when there is no descriptor.
struct Descriptor {
    int port = 0;
    std::string token;
    std::string kind;
    std::string text;
};

inline std::uint32_t this_pid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

inline std::string string_field(const std::string& json, std::string_view name) {
    const std::string key = std::format("\"{}\":\"", name);
    const std::size_t at = json.find(key);
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t begin = at + key.size();
    const std::size_t end = json.find('"', begin);
    return end == std::string::npos ? std::string{} : json.substr(begin, end - begin);
}

inline Descriptor read_descriptor() {
    Descriptor descriptor;
    descriptor.text =
            read_file(engine::cli::descriptor_directory() / (std::to_string(this_pid()) + ".json"));
    const std::size_t port = descriptor.text.find("\"port\":");
    if (port == std::string::npos) {
        return descriptor;
    }
    descriptor.port = std::atoi(descriptor.text.c_str() + port + 7);
    descriptor.token = string_field(descriptor.text, "token");
    descriptor.kind = string_field(descriptor.text, "kind");
    return descriptor;
}

inline Reply post(int port, std::string_view extra, std::string_view body) {
    Reply reply;
#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket invalid = INVALID_SOCKET;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return reply;
    }
#else
    using Socket = int;
    constexpr Socket invalid = -1;
#endif
    const Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == invalid) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return reply;
    }
#if defined(_WIN32)
    const DWORD timeout = 3000;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = 3;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
#if defined(_WIN32)
        closesocket(socket);
        WSACleanup();
#else
        ::close(socket);
#endif
        return reply;
    }
    const std::string request = std::format("POST /exec HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                                            "Content-Length: {}\r\n{}Connection: close\r\n\r\n{}",
            body.size(), extra, body);
    std::string_view pending = request;
    while (!pending.empty()) {
        const int sent = ::send(socket, pending.data(), static_cast<int>(pending.size()), 0);
        if (sent <= 0) {
#if defined(_WIN32)
            closesocket(socket);
            WSACleanup();
#else
            ::close(socket);
#endif
            return reply;
        }
        pending.remove_prefix(static_cast<std::size_t>(sent));
    }
    std::string data;
    char buffer[2048];
    while (true) {
        const int got = ::recv(socket, buffer, sizeof(buffer), 0);
        if (got <= 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(got));
    }
#if defined(_WIN32)
    closesocket(socket);
    WSACleanup();
#else
    ::close(socket);
#endif
    if (data.find("\r\n") == std::string::npos || !data.starts_with("HTTP/1.1 ")) {
        return reply;
    }
    reply.status = std::atoi(data.c_str() + 9);
    const std::size_t split = data.find("\r\n\r\n");
    if (split != std::string::npos) {
        reply.body = data.substr(split + 4);
    }
    return reply;
}

// `post` with this process's token.
inline Reply post_authorized(const Descriptor& descriptor, std::string_view body) {
    return post(descriptor.port, std::format("Authorization: Bearer {}\r\n", descriptor.token), body);
}

}

#endif
