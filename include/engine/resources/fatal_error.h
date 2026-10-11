#pragma once

// docs/tech/modules/Core.md

#include <string_view>

namespace engine {

// The single fatal exit. `report` never returns: an implementation shows the message, shuts the process down
// (exit or abort), or, in a test, throws. Callers may rely on that and write no code after it.
class IFatalError {
public:
    virtual ~IFatalError() = default;
    [[noreturn]] virtual void report(std::string_view message) = 0;
};

}
