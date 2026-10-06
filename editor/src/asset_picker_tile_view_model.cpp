#include "asset_picker_tile_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace editor {

AssetPickerTileViewModel::AssetPickerTileViewModel(SelectCallback on_select)
    : on_select_(std::move(on_select)) {
    assets::ui::AssetPicker::Tiles::bind(*this);
    select.bind_to<AssetPickerTileViewModel, &AssetPickerTileViewModel::select_tile>(*this);
}

void AssetPickerTileViewModel::show(
        std::string name, engine::AssetId guid, engine::AssetId icon_id,
        std::optional<engine::AssetId> thumbnail_id) {
    label = std::move(name);
    guid_ = guid;
    icon = icon_id;
    if (thumbnail_id.has_value()) {
        thumbnail = *thumbnail_id;
        hasThumbnail = true;
        noThumbnail = false;
    } else {
        thumbnail = engine::AssetId{};
        hasThumbnail = false;
        noThumbnail = true;
    }
}

void AssetPickerTileViewModel::select_tile() {
    if (on_select_) {
        on_select_(guid_);
    }
}

} // namespace editor
