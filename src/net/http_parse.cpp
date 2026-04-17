#include "net/http_parse.h"

#include <algorithm>
#include <charconv>

namespace engine {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t';
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(x) == lower(y);
    });
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (is_space(text.back()) || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

}

std::expected<HttpUrl, HttpError> parse_http_url(std::string_view url) {
    const std::unexpected invalid{HttpError::InvalidUrl};
    if (std::ranges::any_of(url, [](char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f; })) {
        return invalid;
    }
    if (const std::size_t hash = url.find('#'); hash != std::string_view::npos) {
        url = url.substr(0, hash);
    }

    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return invalid;
    }
    HttpUrl parsed;
    const std::string_view scheme = url.substr(0, scheme_end);
    if (equals_ignore_case(scheme, "https")) {
        parsed.secure = true;
    } else if (!equals_ignore_case(scheme, "http")) {
        return invalid;
    }

    const std::string_view rest = url.substr(scheme_end + 3);
    const std::size_t authority_end = std::min(rest.find('/'), rest.find('?'));
    const std::string_view authority = rest.substr(0, authority_end);
    const std::string_view target =
            authority_end == std::string_view::npos ? std::string_view{} : rest.substr(authority_end);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return invalid;
    }

    std::string_view host;
    std::string_view port;
    bool has_port = false;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return invalid;
        }
        host = authority.substr(1, close - 1);
        const std::string_view after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return invalid;
            }
            port = after.substr(1);
            has_port = true;
        }
    } else {
        const std::size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            port = authority.substr(colon + 1);
            has_port = true;
        }
    }
    if (host.empty()) {
        return invalid;
    }

    parsed.port = parsed.secure ? 443 : 80;
    if (has_port) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (port.empty() || port.size() > 5 || error != std::errc{} || end != port.data() + port.size() ||
                value == 0 || value > 65535) {
            return invalid;
        }
        parsed.port = static_cast<std::uint16_t>(value);
    }

    parsed.host = std::string(host);
    parsed.target = target.empty() || target.front() == '?' ? "/" + std::string(target) : std::string(target);
    parsed.url = std::string(url);
    return parsed;
}

std::string_view http_method_name(HttpMethod method) {
    switch (method) {
    case HttpMethod::Get:
        return "GET";
    case HttpMethod::Post:
        return "POST";
    case HttpMethod::Put:
        return "PUT";
    case HttpMethod::Patch:
        return "PATCH";
    case HttpMethod::Delete:
        return "DELETE";
    case HttpMethod::Head:
        return "HEAD";
    }
    return "GET";
}

std::vector<HttpHeader> parse_raw_headers(std::string_view raw) {
    std::vector<HttpHeader> headers;
    bool first = true;
    while (!raw.empty()) {
        const std::size_t newline = raw.find('\n');
        std::string_view line = raw.substr(0, newline);
        raw = newline == std::string_view::npos ? std::string_view{} : raw.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const bool status_line = first && line.starts_with("HTTP/");
        first = false;
        const std::size_t colon = line.find(':');
        if (status_line || colon == std::string_view::npos || colon == 0) {
            continue;
        }
        headers.push_back(HttpHeader{
                .name = std::string(trim(line.substr(0, colon))),
                .value = std::string(trim(line.substr(colon + 1))),
        });
    }
    return headers;
}

std::optional<std::string_view> HttpResponse::header(std::string_view name) const {
    const auto found = std::ranges::find_if(
            headers, [name](const HttpHeader& header) { return equals_ignore_case(header.name, name); });
    if (found == headers.end()) {
        return std::nullopt;
    }
    return found->value;
}

}
