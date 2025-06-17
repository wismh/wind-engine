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

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <climits>
#include <cmath>
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
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        if (state.cancelled()) {
            return cancelled;
        }
        DWORD read = 0;
        if (!WinHttpReadData(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            return last_error();
        }
        if (read == 0) {
            break;
        }
        if (response.body.size() + read > kHttpMaxResponseBytes) {
            return std::unexpected(HttpError::TooLarge);
        }
        response.body.append(buffer.data(), read);
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
