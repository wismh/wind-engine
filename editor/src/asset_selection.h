#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace editor {

// A file or folder of the open project, selected in the Project tab. A copy of what the scan saw, so the
// Inspector needs nothing from the Project panel to show it.
struct AssetSelection {
    // ProjectEntry::key: the path under the project directory, generic separators ("assets/ui/menu.xml").
    std::string key;
    // Where it is on disk.
    std::filesystem::path path;
    bool directory = false;
    // Bytes of a file.
    std::uintmax_t size = 0;
    // Entries of a folder that the scan shows.
    std::size_t items = 0;

    bool operator==(const AssetSelection&) const = default;
};

}
