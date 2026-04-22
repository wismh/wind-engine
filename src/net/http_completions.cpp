#include "net/http_completions.h"

#include "net/http_call_state.h"

#include <utility>

namespace engine {

void HttpCompletions::push(std::shared_ptr<HttpCallState> state, HttpResult result) {
    const std::scoped_lock lock(mutex_);
    done_.push_back(Done{.state = std::move(state), .result = std::move(result)});
}

void HttpCompletions::deliver() {
    std::vector<Done> done;
    {
        const std::scoped_lock lock(mutex_);
        done.swap(done_);
    }
    for (Done& entry : done) {
        if (!entry.state->cancelled()) {
            entry.state->result = std::move(entry.result);
        }
    }
}

}
