#include <engine/net/http_client.h>

#include "net/http_call_state.h"
#include "net/http_completions.h"
#include "net/http_parse.h"

#if defined(_WIN32)
#include "net/http_worker_pool.h"
#include "net/winhttp_session.h"
#elif defined(__ANDROID__)
#include "net/android_http.h"
#include "net/http_worker_pool.h"
#elif defined(__EMSCRIPTEN__)
#include "net/web_fetch.h"
#endif

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace engine {
namespace {

#if defined(_WIN32) || defined(__ANDROID__)
constexpr int kWorkerThreads = 4;
#endif

#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
constexpr bool kHasBackend = true;
#else
constexpr bool kHasBackend = false;
#endif

}

struct HttpClient::Impl {
    HttpCompletions completions;
    // Every call not yet finished, so dispose can cancel it. Pruned by poll.
    std::vector<std::weak_ptr<HttpCallState>> live;
    // The backend started. Stays false where there is no backend.
    bool ready = false;

#if defined(_WIN32)
    WinHttpSession session;
    std::optional<HttpWorkerPool> pool;
#elif defined(__ANDROID__)
    AndroidHttp android;
    std::optional<HttpWorkerPool> pool;
#endif

    [[nodiscard]] bool start() {
#if defined(_WIN32)
        if (!session.open()) {
            return false;
        }
        pool.emplace([this](HttpCallState& state, const HttpRequest& request) {
            return session.transfer(state, request);
        }, completions, kWorkerThreads);
        return true;
#elif defined(__ANDROID__)
        if (!android.resolve()) {
            return false;
        }
        pool.emplace([this](HttpCallState& state, const HttpRequest& request) {
            return android.transfer(state, request);
        }, completions, kWorkerThreads);
        return true;
#else
        return true;
#endif
    }

    void stop() {
        for (const std::weak_ptr<HttpCallState>& weak : live) {
            if (const std::shared_ptr<HttpCallState> state = weak.lock()) {
                state->cancel();
            }
        }
        live.clear();
#if defined(_WIN32)
        pool.reset();
        session.close();
#elif defined(__ANDROID__)
        pool.reset();
        android.release();
#endif
    }

    void start_transfer(std::shared_ptr<HttpCallState> state, HttpRequest request, const HttpUrl& url) {
#if defined(_WIN32) || defined(__ANDROID__)
        (void)url;
        pool->submit(std::move(state), std::move(request));
#elif defined(__EMSCRIPTEN__)
        start_web_fetch(std::move(state), request, url, completions);
#else
        (void)request;
        (void)url;
        completions.push(std::move(state), std::unexpected(HttpError::Unsupported));
#endif
    }
};

HttpClient::HttpClient()
    : impl_(std::make_unique<Impl>()) {}

HttpClient::~HttpClient() {
    dispose();
}

bool HttpClient::init() {
    if (impl_->ready) {
        return true;
    }
    if (!impl_->start()) {
        return false;
    }
    impl_->ready = kHasBackend;
    return true;
}

void HttpClient::dispose() {
    impl_->stop();
    impl_->ready = false;
}

HttpCall HttpClient::send(HttpRequest request) {
    auto state = std::make_shared<HttpCallState>();
    HttpCall call{state};
    const auto url = parse_http_url(request.url);
    if (!url) {
        impl_->completions.push(std::move(state), std::unexpected(url.error()));
    } else if (!impl_->ready) {
        impl_->completions.push(std::move(state), std::unexpected(HttpError::Unsupported));
    } else {
        impl_->live.push_back(state);
        impl_->start_transfer(std::move(state), std::move(request), *url);
    }
    return call;
}

bool HttpClient::is_supported() const {
    return impl_->ready;
}

void HttpClient::poll() {
    impl_->completions.deliver();
    std::erase_if(impl_->live, [](const std::weak_ptr<HttpCallState>& weak) {
        const std::shared_ptr<HttpCallState> state = weak.lock();
        return state == nullptr || state->result.has_value() || state->cancelled();
    });
}

}
