#pragma once

#include "asset_picker_view_model.h"
#include "thumbnail_cache.h"

#include <engine/core/window_control.h>
#include <engine/ecs/entity.h>
#include <engine/resources/asset_filter.h>
#include <engine/resources/assets_db.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ecs {
class World;
}

namespace editor {

struct PickerCandidate {
    std::string name;
    engine::AssetId guid;
    std::string relative_path;
    std::filesystem::path full_path;
    std::uintmax_t size = 0;
    std::int64_t modified = 0;
};

class AssetPickerOverlay {
public:
    static constexpr int kCanvasOrder = 95;

    using PickCallback = std::function<void(engine::AssetId)>;

    AssetPickerOverlay();

    AssetPickerOverlay(const AssetPickerOverlay&) = delete;
    AssetPickerOverlay& operator=(const AssetPickerOverlay&) = delete;

    [[nodiscard]] const std::shared_ptr<AssetPickerViewModel>& view_model() const;

    void spawn(engine::ecs::World& world, engine::WindowId window);
    void set_assets(engine::AssetsDb* assets, const std::filesystem::path& project_dir);
    void set_thumbnails(ThumbnailService* thumbnails);

    void open(std::string title, const engine::AssetFilter& filter, PickCallback on_picked);
    void close();
    [[nodiscard]] bool is_open() const noexcept;

    void filter_tiles(std::string_view query);
    void pick(engine::AssetId chosen);

    [[nodiscard]] engine::ecs::Entity canvas() const noexcept { return canvas_; }

private:
    void sync_canvas();
    void populate_candidates(const engine::AssetFilter& filter);
    void update_visible_tiles(std::string_view query);

    std::shared_ptr<AssetPickerViewModel> view_model_;
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity canvas_{};
    engine::AssetsDb* assets_ = nullptr;
    std::filesystem::path project_dir_{};
    ThumbnailService* thumbnails_ = nullptr;
    bool open_ = false;
    PickCallback on_picked_;
    std::vector<PickerCandidate> candidates_;
    std::vector<std::shared_ptr<AssetPickerTileViewModel>> tile_pool_;
};

} // namespace editor
