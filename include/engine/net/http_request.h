#pragma once

// docs/tech/modules/Net.md

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class HttpMethod { Get, Post, Put, Patch, Delete, Head };

struct HttpHeader {
    std::string name{};
    std::string value{};
};

// One piece of a response body, in order. Return false to abort the transfer: the call then ends with
// HttpError::SinkAborted. `chunk` is valid only during the call.
using HttpBodySink = std::function<bool(std::span<const std::byte> chunk)>;

// Where an HttpBodySink runs.
enum class HttpSinkThread {
    // On the thread doing the transfer, as the bytes arrive. Disk writes never touch a frame, and a slow sink
    // slows the download down (nothing is buffered). The sink must not touch main-thread state and must not
    // call back into the engine. `HttpCall::cancel` waits for a running sink to return.
    Worker,
    // On the main thread, inside `HttpClient::poll`, before any result of that frame. Chunks wait in a bounded
    // queue; a full queue pauses the download. Web has no worker threads, so there it is the same as `Worker`.
    Main,
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

    // Redirects the backend follows. 0 returns the 3xx response as it is. Web follows redirects as the browser
    // does and ignores both settings; Android follows up to its platform limit when this is above 0.
    int max_redirects = 10;
    // Follow a redirect from `https://` to `http://`. Windows refuses it by default (`RedirectRefused`);
    // Android and Web decide by themselves.
    bool allow_insecure_redirect = false;

    // Gives up with HttpError::Timeout when the body arrives slower than `min_bytes_per_second` over
    // `stall_seconds` in a row. Either at 0 turns it off. A connection that sends nothing at all is cut by
    // `timeout_seconds`. Not available on Web.
    std::size_t min_bytes_per_second = 0;
    float stall_seconds = 0.f;

    // When set, the body of a 2xx response goes to the sink instead of `HttpResponse::body`, and
    // kHttpMaxResponseBytes does not apply. Any other status keeps its body in `HttpResponse::body`, so an
    // error page never lands in the file a game is writing. Web delivers the whole body in one call.
    HttpBodySink sink{};
    HttpSinkThread sink_thread = HttpSinkThread::Worker;
};

struct HttpResponse {
    // 4xx and 5xx are responses, not errors.
    int status = 0;
    // Names as the server sent them.
    std::vector<HttpHeader> headers{};
    // Empty for a 2xx response when the request had a sink.
    std::string body{};

    // First header whose name matches `name` ignoring ASCII case.
    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
};

// No response at all. A cancelled request has no result, so it is not an error here.
enum class HttpError {
    InvalidUrl,
    // No backend on this platform, or the backend failed to start.
    Unsupported,
    // Anything the more precise codes below do not name, and a browser CORS refusal.
    Network,
    // `timeout_seconds` elapsed, or the transfer stalled (`min_bytes_per_second`).
    Timeout,
    // The body passed kHttpMaxResponseBytes (a request without a sink).
    TooLarge,
    // The host name did not resolve.
    Dns,
    // The certificate or the TLS handshake was refused.
    Tls,
    // The server or the network dropped an established connection.
    ConnectionReset,
    // A redirect was refused: too many, or https to http without `allow_insecure_redirect`.
    RedirectRefused,
    // The sink returned false.
    SinkAborted,
};

inline constexpr std::size_t kHttpMaxResponseBytes = 64u * 1024u * 1024u;

using HttpResult = std::expected<HttpResponse, HttpError>;

// Body bytes received so far and the size the server announced. Counts what the sink or the body got, after
// decompression; `total` is empty when the server sent no Content-Length (or the body is compressed).
struct HttpProgress {
    std::uint64_t received = 0;
    std::optional<std::uint64_t> total{};
};

}
