#include "net/web_fetch.h"

#if defined(__EMSCRIPTEN__)

#include "net/http_call_state.h"
#include "net/http_completions.h"
#include "net/http_parse.h"

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {
namespace {

// Lives from emscripten_fetch until the callback or the cancel, whichever comes first. emscripten keeps
// pointers into it (body, headers) for the whole request.
struct WebFetch {
    std::shared_ptr<HttpCallState> state{};
    HttpCompletions* completions = nullptr;
    HttpMethod method = HttpMethod::Get;
    std::string body{};
    std::vector<std::string> header_text{};
    std::vector<const char*> header_pointers{};
    double started_ms = 0.0;
    double timeout_ms = 0.0;
    // Set by the cancel before it closes the fetch: emscripten may call onerror from inside that close.
    bool closing = false;
};

HttpResult read_result(emscripten_fetch_t* fetch, const WebFetch& job) {
    // Status 0 is no response: DNS, connect, CORS, or the timeout, which the browser does not tell apart.
    if (fetch->status == 0) {
        const bool timed_out = job.timeout_ms > 0.0 && emscripten_get_now() - job.started_ms >= job.timeout_ms;
        return std::unexpected(timed_out ? HttpError::Timeout : HttpError::Network);
    }
    HttpResponse response;
    response.status = fetch->status;
    const std::size_t header_length = emscripten_fetch_get_response_headers_length(fetch);
    if (header_length > 0) {
        std::string raw(header_length + 1, '\0');
        emscripten_fetch_get_response_headers(fetch, raw.data(), raw.size());
        raw.resize(header_length);
        response.headers = parse_raw_headers(raw);
    }
    if (job.method != HttpMethod::Head && fetch->data != nullptr && fetch->numBytes > 0) {
        if (fetch->numBytes > kHttpMaxResponseBytes) {
            return std::unexpected(HttpError::TooLarge);
        }
        response.body.assign(fetch->data, static_cast<std::size_t>(fetch->numBytes));
    }
    return response;
}

// onsuccess (2xx) and onerror (everything else, including 4xx and 5xx with their body).
void on_done(emscripten_fetch_t* fetch) {
    auto* job = static_cast<WebFetch*>(fetch->userData);
    if (job->closing) {
        return;
    }
    job->completions->push(job->state, read_result(fetch, *job));
    (void)job->state->end_transfer();
    emscripten_fetch_close(fetch);
    delete job;
}

}

void start_web_fetch(std::shared_ptr<HttpCallState> state, const HttpRequest& request, const HttpUrl& url,
        HttpCompletions& completions) {
    auto* job = new WebFetch{
            .state = state,
            .completions = &completions,
            .method = request.method,
            .body = request.body,
            .started_ms = emscripten_get_now(),
            .timeout_ms = request.timeout_seconds > 0.f ? std::ceil(request.timeout_seconds * 1000.0) : 0.0,
    };
    for (const HttpHeader& header : request.headers) {
        job->header_text.push_back(header.name);
        job->header_text.push_back(header.value);
    }
    for (const std::string& text : job->header_text) {
        job->header_pointers.push_back(text.c_str());
    }
    job->header_pointers.push_back(nullptr);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    const std::string_view method = http_method_name(request.method);
    std::fill(std::begin(attr.requestMethod), std::end(attr.requestMethod), '\0');
    std::copy(method.begin(), method.end(), attr.requestMethod);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.timeoutMSecs = static_cast<unsigned long>(job->timeout_ms);
    attr.requestHeaders = job->header_pointers.data();
    if (!job->body.empty()) {
        attr.requestData = job->body.data();
        attr.requestDataSize = job->body.size();
    }
    attr.onsuccess = &on_done;
    attr.onerror = &on_done;
    attr.userData = job;

    emscripten_fetch_t* const fetch = emscripten_fetch(&attr, url.url.c_str());
    if (fetch == nullptr) {
        completions.push(std::move(state), std::unexpected(HttpError::Network));
        delete job;
        return;
    }
    (void)state->begin_transfer([job, fetch] {
        job->closing = true;
        emscripten_fetch_close(fetch);
        delete job;
    });
}

}

#endif
