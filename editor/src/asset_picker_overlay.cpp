#include "asset_picker_overlay.h"

#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ecs/world_asset.h>
#include <engine/ui/canvas.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

namespace editor {
namespace {

bool contains_case_insensitive(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    auto it = std::search(text.begin(), text.end(), query.begin(), query.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != text.end();
}

std::string read_object_type(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = engine::ecs::parse_field_table(text);
    if (!parsed) {
        return {};
    }
    const auto type_field = parsed->find("type");
    if (type_field != parsed->end() && type_field->second.as_string()) {
        return *type_field->second.as_string();
    }
    return {};
}

engine::AssetId icon_for_path(std::string_view name) {
    if (name.ends_with(".png")) {
        return assets::icons::file_png;
    }
    if (name.ends_with(".object")) {
        return assets::icons::file_object;
    }
    if (name.ends_with(".css")) {
        return assets::icons::file_css;
    }
    if (name.ends_with(".world")) {
        return assets::icons::file_world;
    }
    if (name.ends_with(".xml")) {
        return assets::icons::file_xml;
    }
    if (name.ends_with(".cpp")) {
        return assets::icons::file_cpp;
    }
    if (name.ends_with(".hpp") || name.ends_with(".h")) {
        return assets::icons::file_hpp;
    }
    return assets::icons::file;
}

} // namespace

AssetPickerOverlay::AssetPickerOverlay()
    : view_model_(std::make_shared<AssetPickerViewModel>(*this)) {}

const std::shared_ptr<AssetPickerViewModel>& AssetPickerOverlay::view_model() const {
    return view_model_;
}

void AssetPickerOverlay::spawn(engine::ecs::World& world, engine::WindowId window) {
    world_ = &world;
    canvas_ = world.create();
    world.emplace<engine::ui::UiCanvas>(canvas_, engine::ui::UiCanvas{
            .document = assets::ui::asset_picker,
            .stylesheet = assets::css::panels,
            .data_context = view_model_,
            .fit = open_ ? engine::ui::UiFit::FillWindow : engine::ui::UiFit::Fixed,
            .order = kCanvasOrder,
            .window = window,
    });
}

void AssetPickerOverlay::set_assets(engine::AssetsDb* assets, const std::filesystem::path& project_dir) {
    assets_ = assets;
    project_dir_ = project_dir;
}

void AssetPickerOverlay::set_thumbnails(ThumbnailService* thumbnails) {
    thumbnails_ = thumbnails;
}

void AssetPickerOverlay::open(std::string title, const engine::AssetFilter& filter, PickCallback on_picked) {
    open_ = true;
    on_picked_ = std::move(on_picked);
    view_model_->title = std::move(title);
    view_model_->search = std::string{};
    populate_candidates(filter);
    update_visible_tiles("");
    sync_canvas();
}

void AssetPickerOverlay::close() {
    open_ = false;
    on_picked_ = nullptr;
    candidates_.clear();
    view_model_->tiles.set({});
    sync_canvas();
}

bool AssetPickerOverlay::is_open() const noexcept {
    return open_;
}

void AssetPickerOverlay::filter_tiles(std::string_view query) {
    if (!open_) {
        return;
    }
    update_visible_tiles(query);
}

void AssetPickerOverlay::pick(engine::AssetId chosen) {
    auto callback = std::move(on_picked_);
    close();
    if (callback) {
        callback(chosen);
    }
}

void AssetPickerOverlay::sync_canvas() {
    if (world_ == nullptr || canvas_ == engine::ecs::Entity{}) {
        return;
    }
    engine::ui::UiCanvas& canvas = world_->get<engine::ui::UiCanvas>(canvas_);
    if (open_) {
        canvas.fit = engine::ui::UiFit::FillWindow;
    } else {
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {};
    }
}

void AssetPickerOverlay::populate_candidates(const engine::AssetFilter& filter) {
    candidates_.clear();

    // The first candidate is always (None) to clear the reference.
    candidates_.push_back(PickerCandidate{
            .name = "(None)",
            .guid = engine::AssetId{},
            .relative_path = "",
            .full_path = {},
    });

    if (assets_ == nullptr) {
        return;
    }

    const auto& catalog = assets_->catalog();
    for (const auto& entry : catalog.entries()) {
        if (filter.importer.has_value() && entry.importer != *filter.importer) {
            continue;
        }

        std::filesystem::path rel_path = entry.relative_path;
        std::filesystem::path full = project_dir_.empty() ? rel_path : (project_dir_ / rel_path);

        if (entry.importer == engine::ImporterKind::Object && !filter.object_type.empty()) {
            const std::string type_name = read_object_type(full);
            if (type_name != filter.object_type) {
                continue;
            }
        }

        std::error_code ec;
        const auto last_mod = std::filesystem::last_write_time(full, ec);
        const auto file_sz = std::filesystem::file_size(full, ec);

        candidates_.push_back(PickerCandidate{
                .name = rel_path.filename().string(),
                .guid = entry.guid,
                .relative_path = entry.relative_path,
                .full_path = std::move(full),
                .size = ec ? 0 : static_cast<std::uintmax_t>(file_sz),
                .modified = ec ? 0 : static_cast<std::int64_t>(last_mod.time_since_epoch().count()),
        });
    }

    if (candidates_.size() > 1) {
        std::sort(candidates_.begin() + 1, candidates_.end(),
                  [](const PickerCandidate& a, const PickerCandidate& b) {
                      return a.name < b.name;
                  });
    }
}

void AssetPickerOverlay::update_visible_tiles(std::string_view query) {
    std::vector<std::shared_ptr<AssetPickerTileViewModel>> next_tiles;

    std::size_t pool_idx = 0;
    for (const auto& cand : candidates_) {
        if (!cand.relative_path.empty() && !contains_case_insensitive(cand.name, query)) {
            continue;
        }

        if (pool_idx >= tile_pool_.size()) {
            tile_pool_.push_back(std::make_shared<AssetPickerTileViewModel>(
                    [this](engine::AssetId id) { pick(id); }));
        }

        auto tile = tile_pool_[pool_idx++];
        const engine::AssetId icon_id = icon_for_path(cand.name);
        std::optional<engine::AssetId> thumbnail_id;

        if (cand.relative_path.ends_with(".png") && thumbnails_ != nullptr) {
            thumbnail_id = thumbnails_->find_or_request(
                    cand.relative_path, cand.full_path, ThumbnailStamp{cand.size, cand.modified});
        }

        tile->show(cand.name, cand.guid, icon_id, thumbnail_id);
        next_tiles.push_back(std::move(tile));
    }

    view_model_->tiles.set(std::move(next_tiles));
}

} // namespace editor
