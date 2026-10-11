#pragma once

// Test doubles for IFatalError. `report` is noreturn, so a fake cannot just record and come back: it throws
// FatalReport, and a test asserts on the report with EXPECT_THROW / ASSERT_THROW.

#include <engine/resources/fatal_error.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace fakes {

struct FatalReport : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Reports nothing but the throw. For services handed an IFatalError the test expects never to be called.
class QuietFatal final : public engine::IFatalError {
public:
    [[noreturn]] void report(std::string_view message) override {
        throw FatalReport(std::string(message));
    }
};

// Counts the reports (a test usually reaches one) and keeps the last message, then throws.
class RecordingFatal final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    [[noreturn]] void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
        throw FatalReport(last_message);
    }
};

}
