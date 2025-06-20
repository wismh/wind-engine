#pragma once

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace engine {

// Results of async calls (HttpCall, FileDialogCall) waiting for the main thread. `push` may run on any
// thread, `deliver` on the main thread only.
//
// `State` has a main-thread `std::optional<Result> result` and a `cancelled()` the main thread can read.
template <typename State, typename Result>
class CallCompletions {
public:
    void push(std::shared_ptr<State> state, Result result) {
        const std::scoped_lock lock(mutex_);
        done_.push_back(Done{.state = std::move(state), .result = std::move(result)});
    }

    // Moves every pushed result into its call, except calls cancelled since.
    void deliver() {
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

private:
    struct Done {
        std::shared_ptr<State> state;
        Result result;
    };

    std::mutex mutex_;
    std::vector<Done> done_;
};

}
