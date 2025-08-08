#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace editor {

// One file or folder of the open project, as the Project tab shows it. A plain copy of what the scan saw:
// nothing here is kept in sync with the disk until the next scan.
struct ProjectEntry {
    std::string name;
    // The path under the project directory, generic separators, UTF-8 ("assets/ui/menu.xml"). Unique, and the
    // same across scans, so the tree keeps its expansion and selection by it.
    std::string key;
    bool directory = false;
    // Bytes of a file; 0 for a folder.
    std::uintmax_t size = 0;
    // Folders first, then files, each by name ignoring case.
    std::vector<ProjectEntry> children;
};

}
