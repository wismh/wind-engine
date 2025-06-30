#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Cuts a child's output bytes into lines. A line ends at '\n'; one '\r' before it is dropped too. Bytes are kept as
// the program wrote them (UTF-8 by convention), so a chunk may end inside a line or inside a character.
class LineSplitter {
public:
    // Appends every line `bytes` completes to `lines` and keeps the unfinished rest for the next call.
    void feed(std::string_view bytes, std::vector<std::string>& lines);

    // The output ended: appends the unfinished rest as the last line, if there is one.
    void finish(std::vector<std::string>& lines);

private:
    std::string partial_;
};

}
