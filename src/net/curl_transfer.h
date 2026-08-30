#pragma once

#include "net/curl_api.h"

#include <engine/net/http_request.h>

namespace engine {

struct HttpCallState;

// One blocking transfer through libcurl, on a worker thread. It needs only the functions in `api`, so it is
// built and tested on every platform with a fake libcurl; the Linux and macOS backend gives it the real one.
//
// libcurl cannot be interrupted from another thread, so a cancel is noticed by the progress callback, which
// libcurl calls at least once a second: while data flows it is immediate, on a silent connection it takes up
// to a second.
[[nodiscard]] HttpResult curl_transfer(const CurlApi& api, HttpCallState& state, const HttpRequest& request);

// libcurl's CURLcode as the engine's error.
[[nodiscard]] HttpError curl_error(int code);

}
