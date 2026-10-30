#pragma once

#include "asset_selection.h"
#include "inspector_section.h"

#include <vector>

namespace engine {
class AssetsDb;
}

namespace editor {

// True when `asset` is a .object file.
[[nodiscard]] bool is_object_asset(const AssetSelection& asset);

// The sections to display for an object asset in the inspector.
[[nodiscard]] std::vector<InspectorSection> inspect_object(
        const AssetSelection& asset, const engine::AssetsDb* assets, bool editable);

}
