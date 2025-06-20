#pragma once

#if defined(__EMSCRIPTEN__)

#include "net/http_completions.h"

#include <engine/net/http_request.h>

#include <memory>

namespace engine {

struct HttpUrl;

// Starts an asynchronous emscripten_fetch. The browser calls back on the main thread between frames and
// the result goes to `completions`, which must outlive the fetch (HttpClient::dispose cancels every call).
// A cancel closes the fetch, which aborts the XMLHttpRequest.
void start_web_fetch(std::shared_ptr<HttpCallState> state, const HttpRequest& request, const HttpUrl& url,
        HttpCompletions& completions);

}

#endif
