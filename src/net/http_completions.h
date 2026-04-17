#pragma once

#include <engine/net/http_request.h>

#include <memory>
#include <mutex>
#include <vector>

namespace engine {

struct HttpCallState;

// Finished transfers waiting for the main thread. `push` may run on any thread, `deliver` on the main
// thread only.
class HttpCompletions {
public:
    void push(std::shared_ptr<HttpCallState> state, HttpResult result);

    // Moves every pushed result into its call, except calls cancelled since.
    void deliver();

private:
    struct Done {
        std::shared_ptr<HttpCallState> state;
        HttpResult result;
    };

    std::mutex mutex_;
    std::vector<Done> done_;
};

}
