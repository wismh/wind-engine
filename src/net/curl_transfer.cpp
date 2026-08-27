#include "net/curl_transfer.h"

#include "net/http_call_state.h"
#include "net/http_parse.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine {
namespace {

using Clock = std::chrono::steady_clock;

struct Context {
    HttpCallState* state = nullptr;
    HttpResponse response{};
    // The header block of the response being read. A redirect or a `100 Continue` starts a new one.
    std::string raw_headers{};
    bool body_started = false;
    // Why the callbacks stopped the transfer, when they did.
    std::optional<HttpError> error{};
    // No data for this long ends the transfer with Timeout. 0 waits forever.
    double idle_seconds = 0.0;
    std::int64_t last_bytes = -1;
    Clock::time_point last_activity = Clock::now();
};

int status_of(std::string_view status_line) {
    const std::size_t space = status_line.find(' ');
    if (space == std::string_view::npos) {
        return 0;
    }
    status_line.remove_prefix(space + 1);
    int status = 0;
    std::from_chars(status_line.data(), status_line.data() + status_line.size(), status);
    return status;
}

std::size_t on_header(char* data, std::size_t size, std::size_t count, void* user) {
    auto& context = *static_cast<Context*>(user);
    const std::string_view line(data, size * count);
    if (line.starts_with("HTTP/")) {
        context.raw_headers.clear();
        context.response.status = status_of(line);
    }
    context.raw_headers.append(line);
    return size * count;
}

// The headers are complete when the first body byte arrives (or the transfer ends with no body).
void finish_headers(Context& context) {
    context.response.headers = parse_raw_headers(context.raw_headers);
    // A compressed body is decoded as it arrives, so Content-Length would not match what is counted.
    if (!context.response.header("Content-Encoding")) {
        if (const std::optional<std::uint64_t> total = content_length(context.response)) {
            context.state->set_total(*total);
        }
    }
}

std::size_t on_write(char* data, std::size_t size, std::size_t count, void* user) {
    auto& context = *static_cast<Context*>(user);
    if (!context.body_started) {
        context.body_started = true;
        finish_headers(context);
    }
    const std::size_t length = size * count;
    if (const std::optional<HttpError> error = context.state->accept(
                context.response.status, std::as_bytes(std::span(data, length)), context.response.body)) {
        context.error = *error;
        return 0;
    }
    return length;
}

// Nonzero stops the transfer. Called at least once a second, with or without data.
int on_progress(void* user, std::int64_t, std::int64_t downloaded, std::int64_t, std::int64_t uploaded) {
    auto& context = *static_cast<Context*>(user);
    if (context.state->cancelled()) {
        context.error = HttpError::Network;
        return 1;
    }
    const auto now = Clock::now();
    const std::int64_t bytes = downloaded + uploaded;
    if (bytes != context.last_bytes) {
        context.last_bytes = bytes;
        context.last_activity = now;
    } else if (context.idle_seconds > 0.0 &&
               std::chrono::duration<double>(now - context.last_activity).count() > context.idle_seconds) {
        context.error = HttpError::Timeout;
        return 1;
    }
    return 0;
}

class Easy {
public:
    Easy(const CurlApi& api, void* handle)
        : api_(api)
        , handle_(handle) {}
    Easy(const Easy&) = delete;
    Easy& operator=(const Easy&) = delete;
    ~Easy() {
        if (headers_ != nullptr) {
            api_.slist_free_all(headers_);
        }
        api_.easy_cleanup(handle_);
    }

    void set_long(int option, long value) const {
        api_.easy_setopt(handle_, option, value);
    }
    void set_offset(int option, std::int64_t value) const {
        api_.easy_setopt(handle_, option, value);
    }
    void set_pointer(int option, const void* value) const {
        api_.easy_setopt(handle_, option, value);
    }
    template <typename Function>
    void set_function(int option, Function* value) const {
        api_.easy_setopt(handle_, option, value);
    }

    void add_header(const std::string& line) {
        void* const grown = api_.slist_append(headers_, line.c_str());
        if (grown != nullptr) {
            headers_ = grown;
        }
    }
    void send_headers() const {
        if (headers_ != nullptr) {
            api_.easy_setopt(handle_, curl::kHttpHeader, headers_);
        }
    }
    [[nodiscard]] int perform() const {
        return api_.easy_perform(handle_);
    }

private:
    const CurlApi& api_;
    void* handle_;
    void* headers_ = nullptr;
};

long seconds_up(float seconds) {
    return static_cast<long>(std::clamp(std::ceil(static_cast<double>(seconds)), 1.0, static_cast<double>(INT_MAX)));
}

}

HttpError curl_error(int code) {
    switch (code) {
    case curl::kCouldntResolveHost:
    case curl::kCouldntResolveProxy:
        return HttpError::Dns;
    case curl::kOperationTimedOut:
        return HttpError::Timeout;
    case curl::kTooManyRedirects:
    // libcurl answers a redirect to a protocol REDIR_PROTOCOLS does not allow this way; the first URL is checked
    // before the call, so this can only be a redirect.
    case curl::kUnsupportedProtocol:
        return HttpError::RedirectRefused;
    case curl::kSslConnectError:
    case curl::kPeerFailedVerification:
    case curl::kSslCertProblem:
    case curl::kSslCipher:
    case curl::kUseSslFailed:
    case curl::kSslEngineInitFailed:
    case curl::kSslCaCertBadFile:
    case curl::kSslCrlBadFile:
    case curl::kSslIssuerError:
    case curl::kSslClientCert:
        return HttpError::Tls;
    case curl::kGotNothing:
    case curl::kSendError:
    case curl::kRecvError:
    case curl::kPartialFile:
        return HttpError::ConnectionReset;
    default:
        return HttpError::Network;
    }
}

HttpResult curl_transfer(const CurlApi& api, HttpCallState& state, const HttpRequest& request) {
    const auto url = parse_http_url(request.url);
    if (!url) {
        return std::unexpected(url.error());
    }
    void* const handle = api.easy_init();
    if (handle == nullptr) {
        return std::unexpected(HttpError::Network);
    }
    Easy easy(api, handle);
    Context context;
    context.state = &state;
    context.idle_seconds = request.timeout_seconds > 0.f ? static_cast<double>(request.timeout_seconds) : 0.0;

    const std::string url_text = url->url;
    easy.set_pointer(curl::kUrl, url_text.c_str());
    easy.set_pointer(curl::kUserAgent, "Wind");
    easy.set_long(curl::kNoSignal, 1L);
    easy.set_long(curl::kProtocols, curl::kProtoHttp | curl::kProtoHttps);
    easy.set_long(curl::kBufferSize, 64L * 1024L);
    // The empty string asks for every encoding libcurl can decode and decodes the body itself.
    easy.set_pointer(curl::kAcceptEncoding, "");

    // A connect that takes longer than the timeout fails; a transfer that goes quiet for that long is cut by
    // the progress callback.
    easy.set_long(curl::kConnectTimeoutMs,
            request.timeout_seconds > 0.f ? static_cast<long>(std::ceil(static_cast<double>(request.timeout_seconds) * 1000.0))
                                          : 0L);
    if (request.min_bytes_per_second > 0 && request.stall_seconds > 0.f) {
        easy.set_long(curl::kLowSpeedLimit, static_cast<long>(request.min_bytes_per_second));
        easy.set_long(curl::kLowSpeedTime, seconds_up(request.stall_seconds));
    }

    // Never from https down to http, unless asked. A plain http start may go to either.
    easy.set_long(curl::kFollowLocation, request.max_redirects > 0 ? 1L : 0L);
    easy.set_long(curl::kMaxRedirs, static_cast<long>(std::max(request.max_redirects, 0)));
    easy.set_long(curl::kRedirProtocols,
            url->secure && !request.allow_insecure_redirect ? curl::kProtoHttps : curl::kProtoHttp | curl::kProtoHttps);

    const std::string method(http_method_name(request.method));
    if (request.method == HttpMethod::Head) {
        easy.set_long(curl::kNoBody, 1L);
    } else if (request.method != HttpMethod::Get || !request.body.empty()) {
        easy.set_pointer(curl::kCustomRequest, method.c_str());
    }
    if (!request.body.empty()) {
        easy.set_pointer(curl::kPostFields, request.body.data());
        easy.set_offset(curl::kPostFieldSizeLarge, static_cast<std::int64_t>(request.body.size()));
    }
    for (const HttpHeader& header : request.headers) {
        easy.add_header(header.name + ": " + header.value);
    }
    easy.send_headers();

    easy.set_function(curl::kHeaderFunction, &on_header);
    easy.set_pointer(curl::kHeaderData, static_cast<const void*>(&context));
    easy.set_function(curl::kWriteFunction, &on_write);
    easy.set_pointer(curl::kWriteData, static_cast<const void*>(&context));
    easy.set_long(curl::kNoProgress, 0L);
    easy.set_function(curl::kXferInfoFunction, &on_progress);
    easy.set_pointer(curl::kXferInfoData, static_cast<const void*>(&context));

    // libcurl is not safe to touch from another thread, so there is nothing for a cancel to run: the
    // progress callback watches `cancelled()`.
    if (!state.begin_transfer([] {})) {
        return std::unexpected(HttpError::Network);
    }
    const int code = easy.perform();
    (void)state.end_transfer();

    if (context.error) {
        return std::unexpected(*context.error);
    }
    if (code != curl::kOk) {
        return std::unexpected(curl_error(code));
    }
    if (!context.body_started) {
        finish_headers(context);
    }
    return std::move(context.response);
}

}
