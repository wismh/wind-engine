#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine::ui {

// Ages toward total_duration; the entity is destroyed once elapsed >= total_duration (an engine
// system does this — see register_engine_systems). Uses Time::delta_time (not a fixed-step tick),
// matching every other UI animation in this engine (SDD §20.3) — it keeps advancing even while
// ApplicationState::paused, same as before this change (Schedule::Frame always runs, §4.6).
struct SplashTimer {
    float elapsed = 0.0f;
    float total_duration = 0.0f;
};

// Spawns a full-screen splash UiCanvas + UiInstance into `window` (default kPrimaryWindow) — a
// game calls this explicitly (typically from on_start(), but not required to be there; it can be
// called later, e.g. right after opening a secondary window, targeting that window instead).
// `image_size` is the configured image's real decoded pixel size (get it from
// AssetsDb::get<render::TextureDesc>(config.image) — width/height — the caller already has
// AssetsDb via DI, same as loading any other texture). Returns nullopt when there's nothing to
// show: config.enabled is false, or the underlying document couldn't be built (SDD §20.3 — e.g.
// all-zero durations, unresolved image size). The returned entity carries UiCanvas + UiInstance +
// SplashTimer; an engine system despawns it automatically once SplashTimer.total_duration elapses
// — the caller does not need to do anything else.
[[nodiscard]] std::optional<ecs::Entity> show_splash(
        ecs::World& world, const SplashScreen& config, glm::vec2 image_size, WindowId window = kPrimaryWindow);

}
