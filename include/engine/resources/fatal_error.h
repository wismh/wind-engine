#pragma once

#include <string_view>

namespace engine {

class IFatalError {
public:
    virtual ~IFatalError() = default;
    virtual void report(std::string_view message) = 0;
};

}
