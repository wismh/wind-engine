#pragma once

#include <engine/net/http_request.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

struct HttpUrl {
    bool secure = false;
    // IPv6 literals without their brackets.
    std::string host;
    std::uint16_t port = 0;
    // Path and query, always starting with '/'.
    std::string target;
    // The input without its fragment.
    std::string url;
};

// http:// or https:// (scheme case ignored), a non-empty host, an optional port 1-65535, no user info,
// no whitespace or control characters. Anything else is HttpError::InvalidUrl.
[[nodiscard]] std::expected<HttpUrl, HttpError> parse_http_url(std::string_view url);

[[nodiscard]] std::string_view http_method_name(HttpMethod method);

// `Name: value` lines split by CRLF or LF. A leading `HTTP/` status line and lines without a colon are
// skipped. Whitespace around the value is trimmed.
[[nodiscard]] std::vector<HttpHeader> parse_raw_headers(std::string_view raw);

}
