#pragma once

// docs/tech/modules/Core.md

#include <engine/builtin_ids.h>
#include <engine/core/window_desc.h>
#include <engine/core/worlds.h>
#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>

#include <optional>

namespace engine {

// Three phases instead of one duration_seconds: fade-in and fade-out need to be timed
// independently from the hold in the middle.
struct SplashScreen {
    bool enabled = true;
    AssetId image = builtin::splash_wind;
    float fade_in_seconds = 0.4f;
    float hold_seconds = 1.0f;
    float fade_out_seconds = 0.4f;
};

class IGame {
public:
    virtual ~IGame() = default;

    // Only the primary window is declared up front; any further window is opened later
    // through IWindowControl.
    virtual WindowDesc primary_window() const {
        return {};
    }

    // No default icon: unset means the OS/window-manager default is used.
    virtual std::optional<AssetId> window_icon() const {
        return std::nullopt;
    }

    virtual ecs::World& world() = 0;
    virtual void on_start() = 0;
    virtual void on_quit() = 0;
};

class GameBase : public IGame {
public:
    explicit GameBase(Worlds& worlds)
        : worlds_(&worlds)
        , world_(&worlds.add()) {}

    ecs::World& world() override {
        return *world_;
    }

    [[nodiscard]] Worlds& worlds() {
        return *worlds_;
    }

    void on_start() override {}

    void on_quit() override {}

private:
    Worlds* worlds_ = nullptr;
    ecs::World* world_ = nullptr;
};

}
