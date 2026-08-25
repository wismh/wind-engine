#pragma once

#include <string>
#include <vector>

namespace editor {

// One section of the Inspector tab as plain data: a heading and its lines, one row each. The panel turns it
// into view-models and remembers by heading which sections the user collapsed.
struct InspectorSection {
    std::string heading;
    std::vector<std::string> lines;

    bool operator==(const InspectorSection&) const = default;
};

}
