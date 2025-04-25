#pragma once

// docs/tech/modules/Net.md

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class HttpMethod { Get, Post, Put, Patch, Delete, Head };

struct HttpHeader {
    std::string name{};
    std::string value{};
};

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    // `http://` or `https://`, host required, no user info. A fragment is dropped.
    std::string url{};
    std::vector<HttpHeader> headers{};
    // Raw bytes. Sent as is, with no Content-Type unless `headers` names one.
    std::string body{};
    // <= 0 waits forever. Windows and Android apply it to connect and to each read; Web to the whole request.
    float timeout_seconds = 30.f;
};

struct HttpResponse {
    // 4xx and 5xx are responses, not errors.
    int status = 0;
    // Names as the server sent them.
    std::vector<HttpHeader> headers{};
    std::string body{};

    // First header whose name matches `name` ignoring ASCII case.
    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
};

// No response at all. A cancelled request has no result, so it is not an error here.
enum class HttpError {
    InvalidUrl,
    // No backend on this platform, or the backend failed to start.
    Unsupported,
    // DNS, connect, TLS, a dropped connection, or a browser CORS refusal.
    Network,
    Timeout,
    // The body passed kHttpMaxResponseBytes.
    TooLarge,
};

inline constexpr std::size_t kHttpMaxResponseBytes = 64u * 1024u * 1024u;

using HttpResult = std::expected<HttpResponse, HttpError>;

}
