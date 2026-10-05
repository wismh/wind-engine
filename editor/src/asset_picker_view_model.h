#pragma once

#include "asset_picker_tile_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

class AssetPickerOverlay;

class AssetPickerViewModel final : public engine::ui::ViewModel {
public:
    explicit AssetPickerViewModel(AssetPickerOverlay& overlay);

    AssetPickerViewModel(const AssetPickerViewModel&) = delete;
    AssetPickerViewModel& operator=(const AssetPickerViewModel&) = delete;

    engine::ui::Bindable<std::string> title;
    engine::ui::Bindable<std::string> search;
    engine::ui::BindableList<std::shared_ptr<AssetPickerTileViewModel>> tiles;
    engine::ui::MethodCommand close;

    void on_close();
    void on_search_changed(const std::string& query);

private:
    AssetPickerOverlay* overlay_;
};

} // namespace editor
