#pragma once

// docs/tech/modules/Core.md

#include <engine/core/export.h>
#include <engine/resources/fatal_error.h>

#include <string_view>

namespace engine {

// Writes the message to stderr and aborts. Used where no windowed host supplied an IFatalError.
class DefaultFatalError final : public IFatalError {
public:
    [[noreturn]] void report(std::string_view message) override;
};

// The process-wide DefaultFatalError.
[[nodiscard]] ENGINE_API IFatalError& default_fatal_error();

}
