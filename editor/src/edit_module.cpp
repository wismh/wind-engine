#include "edit_module.h"

#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/log.h>
#include <engine/resources/assets_db.h>

#include <utility>

namespace editor {
namespace {

struct WorldSerializerAnchor {
    std::shared_ptr<engine::ecs::WorldSerializer*> slot;

    ~WorldSerializerAnchor() {
        if (slot) {
            *slot = nullptr;
        }
    }
};

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace

EditModule::EditModule(IPlayHost& host, engine::AssetsDb& assets, std::filesystem::path live_root) :
    host_(&host), assets_(&assets), live_root_(std::move(live_root)) {}

EditModule::~EditModule() { unload(); }

std::expected<void, std::string> EditModule::load(const std::filesystem::path& module) {
    unload();
    auto loaded = engine::load_game_module(module, live_root_);
    if (!loaded) {
        return std::unexpected(engine::describe(loaded.error()));
    }
    std::filesystem::path assets_dir = module.parent_path() / "assets";
    if (auto catalog = host_->load_catalog(assets_dir); !catalog) {
        return std::unexpected(catalog.error());
    }
    module_.emplace(std::move(*loaded));
    assets_dir_ = std::move(assets_dir);
    last_module_path_ = module;
    module_->register_objects(*assets_);
    engine::log::info("Editor: edit module " + path_text(module) + " from " + path_text(module_->live_path()));
    return {};
}

void EditModule::unload(engine::ecs::World* edit_world) {
    if (edit_world != nullptr) {
        edit_world->ctx<engine::ecs::WorldSerializer>() = engine::ecs::WorldSerializer{};
    }
    for (const std::weak_ptr<engine::ecs::WorldSerializer*>& weak: tracked_worlds_) {
        if (const std::shared_ptr<engine::ecs::WorldSerializer*> slot = weak.lock(); slot && *slot != nullptr) {
            **slot = engine::ecs::WorldSerializer{};
        }
    }
    tracked_worlds_.clear();
    if (!module_) {
        return;
    }
    host_->unload_catalog(assets_dir_);
    assets_->clear_object_loaders();
    module_.reset();
    assets_dir_.clear();
}

void EditModule::reset(engine::ecs::World* edit_world) {
    unload(edit_world);
    last_module_path_.clear();
}

void EditModule::remember_module(std::filesystem::path module) { last_module_path_ = std::move(module); }

void EditModule::register_components(engine::ecs::WorldSerializer& serializer) const {
    serializer = engine::ecs::WorldSerializer{};
    if (!module_) {
        return;
    }
    module_->register_components(serializer);
}

void EditModule::register_components(engine::ecs::World& world) const {
    engine::ecs::WorldSerializer& serializer = world.ctx<engine::ecs::WorldSerializer>();
    register_components(serializer);
    if (!module_) {
        return;
    }
    WorldSerializerAnchor& anchor = world.ctx<WorldSerializerAnchor>();
    if (!anchor.slot) {
        anchor.slot = std::make_shared<engine::ecs::WorldSerializer*>(&serializer);
    } else {
        *anchor.slot = &serializer;
    }
    tracked_worlds_.push_back(anchor.slot);
}

bool EditModule::loaded() const noexcept { return module_.has_value(); }

const std::filesystem::path& EditModule::module_path() const noexcept { return last_module_path_; }

} // namespace editor
