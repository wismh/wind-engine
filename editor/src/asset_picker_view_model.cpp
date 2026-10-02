#include "asset_picker_view_model.h"

#include "asset_picker_overlay.h"

#include <asset_ids.h>

namespace editor {

AssetPickerViewModel::AssetPickerViewModel(AssetPickerOverlay& overlay) : overlay_(&overlay) {
    assets::ui::AssetPicker::bind(*this);
    close.bind_to<AssetPickerViewModel, &AssetPickerViewModel::on_close>(*this);
    search.on_changed([this](const std::string& text) { on_search_changed(text); });
}

void AssetPickerViewModel::on_close() {
    if (overlay_ != nullptr) {
        overlay_->close();
    }
}

void AssetPickerViewModel::on_search_changed(const std::string& query) {
    if (overlay_ != nullptr) {
        overlay_->filter_tiles(query);
    }
}

} // namespace editor
