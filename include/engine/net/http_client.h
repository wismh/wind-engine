#pragma once

// docs/tech/modules/Net.md

#include <engine/net/http_call.h>
#include <engine/net/http_request.h>

#include <memory>

namespace engine {

// HTTP and HTTPS requests. One frontend; the platform backend is hidden from game code:
//   - Windows: WinHTTP on a small worker pool.
//   - Android: java.net.HttpURLConnection through JNI on the same worker pool. Needs the INTERNET
//              permission (the engine manifest has it). Cleartext http:// is blocked from API 28 unless
//              the game's manifest allows it.
//   - Web:     emscripten_fetch. The browser applies CORS.
//   - Others:  every request answers HttpError::Unsupported. is_supported() == false.
//
// `send` returns at once. The answer lands in the returned HttpCall during a later frame. Main thread only.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    virtual bool init() = 0;
    // Cancels every request still in flight.
    virtual void dispose() = 0;

    [[nodiscard]] virtual HttpCall send(HttpRequest request) = 0;

    [[nodiscard]] virtual bool is_supported() const = 0;
};

class HttpClient final : public IHttpClient {
public:
    HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    ~HttpClient() override;

    bool init() override;
    void dispose() override;
    [[nodiscard]] HttpCall send(HttpRequest request) override;
    [[nodiscard]] bool is_supported() const override;

    // Hands finished transfers to their calls. The game loop runs it once per frame, before simulation.
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
