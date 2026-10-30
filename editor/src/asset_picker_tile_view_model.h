#pragma once

#include <engine/resources/asset_id.h>
#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <functional>
#include <optional>
#include <string>

namespace editor {

class AssetPickerTileViewModel final : public engine::ui::ViewModel {
public:
    using SelectCallback = std::function<void(engine::AssetId)>;

    explicit AssetPickerTileViewModel(SelectCallback on_select);

    AssetPickerTileViewModel(const AssetPickerTileViewModel&) = delete;
    AssetPickerTileViewModel& operator=(const AssetPickerTileViewModel&) = delete;

    void show(std::string name, engine::AssetId guid, engine::AssetId icon_id,
              std::optional<engine::AssetId> thumbnail_id);

    void select_tile();

    engine::ui::Bindable<std::string> label;
    engine::ui::Bindable<engine::AssetId> icon;
    engine::ui::Bindable<engine::AssetId> thumbnail;
    engine::ui::Bindable<bool> hasThumbnail{false};
    engine::ui::Bindable<bool> noThumbnail{true};
    engine::ui::MethodCommand select;

    [[nodiscard]] engine::AssetId guid() const noexcept { return guid_; }

private:
    SelectCallback on_select_;
    engine::AssetId guid_{};
};

} // namespace editor
