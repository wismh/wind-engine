#pragma once

// docs/tech/modules/Net.md

#include <engine/net/http_request.h>

#include <memory>
#include <optional>

namespace engine {

struct HttpCallState;

// One request in flight, owned by whoever sent it. Move-only. Destroying a call that is still
// pending cancels it, so a call kept in a component, a view-model, or a game object dies with its owner.
//
// The result becomes visible at the start of a frame (`HttpClient::poll`), so every system of that frame
// sees the same state. Main thread only.
class HttpCall {
public:
    HttpCall() = default;
    // Engine side: `IHttpClient` implementations build calls from their own state.
    explicit HttpCall(std::shared_ptr<HttpCallState> state);

    HttpCall(const HttpCall&) = delete;
    HttpCall& operator=(const HttpCall&) = delete;
    HttpCall(HttpCall&& other) noexcept = default;
    HttpCall& operator=(HttpCall&& other) noexcept;
    ~HttpCall();

    // A call that already holds `result`. For fakes of IHttpClient.
    [[nodiscard]] static HttpCall resolved(HttpResult result);

    // Sent, not cancelled, and no result yet.
    [[nodiscard]] bool pending() const;

    // The result once, then the call is empty. nullopt while pending, after cancel, and on an empty call.
    [[nodiscard]] std::optional<HttpResult> take();

    // Aborts the transfer. No result arrives afterwards. An empty or finished call ignores it.
    void cancel();

private:
    std::shared_ptr<HttpCallState> state_;
};

}
