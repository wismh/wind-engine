#pragma once

// Test double for IBindErrorSink: apply_bindings keeps walking after a failure, so the sink records and returns.

#include <engine/ui/document.h>

#include <string>
#include <string_view>
#include <vector>

namespace fakes {

class RecordingBindErrors final : public engine::ui::IBindErrorSink {
public:
    std::vector<std::string> messages;
    int call_count = 0;
    std::string last_message;

    void bind_error(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
        messages.emplace_back(message);
    }
};

}
