#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "net/winhttp_session.h"

#if defined(_WIN32)

#include "net/http_call_state.h"
#include "net/http_parse.h"
#include "net/http_stall.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

HttpError error_from(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
        return HttpError::Timeout;
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
        return HttpError::InvalidUrl;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return HttpError::Dns;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
        return HttpError::Tls;
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return HttpError::ConnectionReset;
    case ERROR_WINHTTP_REDIRECT_FAILED:
        return HttpError::RedirectRefused;
    default:
        return HttpError::Network;
    }
}

std::unexpected<HttpError> last_error() {
    return std::unexpected(error_from(GetLastError()));
}

int timeout_ms(float seconds) {
    if (seconds <= 0.f) {
        return 0;
    }
    const double ms = std::ceil(static_cast<double>(seconds) * 1000.0);
    return static_cast<int>(std::clamp(ms, 1.0, static_cast<double>(INT_MAX)));
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::wstring header_block(const std::vector<HttpHeader>& headers) {
    std::string block;
    for (const HttpHeader& header : headers) {
        block += header.name;
        block += ": ";
        block += header.value;
        block += "\r\n";
    }
    return widen(block);
}

// Every WinHTTP call checks `cancelled` first: once a cancel closed the handle, the value must not be used.
HttpResult exchange(HttpCallState& state, HINTERNET handle, const HttpRequest& request) {
    const std::unexpected cancelled{HttpError::Network};

    const int ms = timeout_ms(request.timeout_seconds);
    WinHttpSetTimeouts(handle, ms, ms, ms, ms);

    // Redirects are the caller's call. WinHTTP's own default (up to 10, never https to http) is spelled out.
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (request.max_redirects <= 0) {
        redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    } else if (request.allow_insecure_redirect) {
        redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    }
    WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));
    DWORD max_redirects = static_cast<DWORD>(std::max(request.max_redirects, 0));
    WinHttpSetOption(handle, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &max_redirects, sizeof(max_redirects));
#if defined(WINHTTP_OPTION_DECOMPRESSION)
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(handle, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
#endif

    const std::wstring headers = header_block(request.headers);
    const auto body_size = static_cast<DWORD>(request.body.size());
    void* const body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
    if (state.cancelled()) {
        return cancelled;
    }
    if (!WinHttpSendRequest(handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                headers.empty() ? 0 : static_cast<DWORD>(-1L), body, body_size, body_size, 0)) {
        return last_error();
    }
    if (state.cancelled()) {
        return cancelled;
    }
    if (!WinHttpReceiveResponse(handle, nullptr)) {
        return last_error();
    }

    HttpResponse response;
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (state.cancelled()) {
        return cancelled;
    }
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        return last_error();
    }
    response.status = static_cast<int>(status);

    DWORD header_bytes = 0;
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
            &header_bytes, WINHTTP_NO_HEADER_INDEX);
    if (header_bytes > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::wstring raw(header_bytes / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
                    &header_bytes, WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(header_bytes / sizeof(wchar_t));
            response.headers = parse_raw_headers(narrow(raw));
        }
    }

    if (request.method == HttpMethod::Head) {
        return response;
    }
    if (const std::optional<std::uint64_t> total = content_length(response)) {
        state.set_total(*total);
    }
    std::vector<char> buffer(64 * 1024);
    StallWatch stall(request.min_bytes_per_second, request.stall_seconds, now_seconds());
    for (;;) {
        if (state.cancelled()) {
            return cancelled;
        }
        // WinHttpReadData waits until its whole buffer is full, which stalls progress and the stall check on a
        // slow body. Asking for what has arrived keeps every read short.
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle, &available)) {
            return last_error();
        }
        if (available == 0) {
            break;
        }
        DWORD read = 0;
        if (!WinHttpReadData(handle, buffer.data(), std::min(available, static_cast<DWORD>(buffer.size())), &read)) {
            return last_error();
        }
        if (read == 0) {
            break;
        }
        if (const std::optional<HttpError> error =
                        state.accept(response.status, std::as_bytes(std::span(buffer.data(), read)), response.body)) {
            return std::unexpected(*error);
        }
        if (stall.stalled(now_seconds(), state.received())) {
            return std::unexpected(HttpError::Timeout);
        }
    }
    return response;
}

}

WinHttpSession::~WinHttpSession() {
    close();
}

bool WinHttpSession::open() {
    close();
#if defined(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
    session_ = WinHttpOpen(
            L"Wind", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
#endif
    if (session_ == nullptr) {
        session_ = WinHttpOpen(
                L"Wind", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    return session_ != nullptr;
}

void WinHttpSession::close() {
    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
}

HttpResult WinHttpSession::transfer(HttpCallState& state, const HttpRequest& request) const {
    const auto url = parse_http_url(request.url);
    if (!url) {
        return std::unexpected(url.error());
    }
    const std::wstring host = widen(url->host);
    HINTERNET const connect = WinHttpConnect(session_, host.c_str(), url->port, 0);
    if (connect == nullptr) {
        return last_error();
    }
    const std::wstring method = widen(http_method_name(request.method));
    const std::wstring target = widen(url->target);
    HINTERNET const handle = WinHttpOpenRequest(connect, method.c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, url->secure ? WINHTTP_FLAG_SECURE : 0);
    if (handle == nullptr) {
        const DWORD code = GetLastError();
        WinHttpCloseHandle(connect);
        return std::unexpected(error_from(code));
    }

    // Closing the request handle from the main thread is how WinHTTP aborts a blocking call.
    HttpResult result = std::unexpected(HttpError::Network);
    if (state.begin_transfer([handle] { WinHttpCloseHandle(handle); })) {
        result = exchange(state, handle, request);
        if (!state.end_transfer()) {
            WinHttpCloseHandle(handle);
        }
    } else {
        WinHttpCloseHandle(handle);
    }
    WinHttpCloseHandle(connect);
    return result;
}

}

#endif
