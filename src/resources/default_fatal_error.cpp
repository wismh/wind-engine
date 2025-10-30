#include <engine/resources/default_fatal_error.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace engine {

void DefaultFatalError::report(std::string_view message) {
    const std::string text(message);
    std::fprintf(stderr, "Fatal: %s\n", text.c_str());
    std::fflush(stderr);
    std::abort();
}

IFatalError& default_fatal_error() {
    static DefaultFatalError instance;
    return instance;
}

}
