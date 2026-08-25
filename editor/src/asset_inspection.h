#pragma once

#include "asset_selection.h"
#include "inspector_section.h"

#include <cstddef>
#include <vector>

namespace editor {

// Lines of a text file the Content section shows at most, and the bytes it reads for them.
inline constexpr std::size_t kMaxContentLines = 500;
inline constexpr std::size_t kMaxContentBytes = 64 * 1024;

// What the Inspector shows for a file or folder of the project, read from disk now:
// - Folder or File: the path, then the size, or the item count of a folder.
// - Import, when the file has a `.meta` sidecar: its GUID and importer, and the texture or audio settings, or
//   why the sidecar does not read.
// - Content: a PNG's size, or the first lines of a text file (no NUL byte in what was read), tabs as 4
//   spaces. A binary file has no Content section.
[[nodiscard]] std::vector<InspectorSection> inspect_asset(const AssetSelection& asset);

}
