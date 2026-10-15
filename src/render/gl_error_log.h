#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::render {

// "GL_INVALID_OPERATION" for a glGetError code; "unknown GL error" for a code GL 3.3 and ES 3.0 do not define.
[[nodiscard]] std::string_view gl_error_name(unsigned int code);

// The log lines for GL errors found after a draw. A broken draw fails the same way every frame, so each error is
// reported once per place it was found, not 60 times a second.
class GlErrorLog {
public:
    // The line to log for `code` found after `where` (a command name), or empty when that pair was reported already.
    [[nodiscard]] std::string report(unsigned int code, std::string_view where);

private:
    std::vector<std::pair<unsigned int, std::string>> reported_;
};

}
