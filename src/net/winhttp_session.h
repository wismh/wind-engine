#pragma once

#if defined(_WIN32)

#include <engine/net/http_request.h>

namespace engine {

struct HttpCallState;

// One WinHTTP session handle shared by every transfer. `transfer` is blocking and runs on a worker; a
// cancel closes the request handle, which makes the blocking call return.
class WinHttpSession {
public:
    WinHttpSession() = default;
    WinHttpSession(const WinHttpSession&) = delete;
    WinHttpSession& operator=(const WinHttpSession&) = delete;
    ~WinHttpSession();

    [[nodiscard]] bool open();
    void close();

    [[nodiscard]] HttpResult transfer(HttpCallState& state, const HttpRequest& request) const;

private:
    void* session_ = nullptr;
};

}

#endif
