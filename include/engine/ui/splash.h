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

// Spawns a full-screen splash into `window` (default kPrimaryWindow) — a game calls this
// explicitly (typically from on_start(), but not required to be there; it can be called later,
// e.g. right after opening a secondary window, targeting that window instead). Two UiCanvas +
// UiInstance entities are spawned, not one: an opaque black UiFit::FillWindow backdrop (always
// covers the whole window, whatever its aspect ratio) and a UiFit::ScaleWithScreenSize image
// layer drawn on top of it (letterboxes to the configured image's own aspect ratio) — see
// src/ui/splash.cpp's build_splash_document for why a single canvas can't do both. `image_size`
// is the configured image's real decoded pixel size (get it from
// AssetsDb::get<render::TextureDesc>(config.image) — width/height — the caller already has
// AssetsDb via DI, same as loading any other texture). Returns nullopt when there's nothing to
// show: config.enabled is false, or the underlying document couldn't be built (SDD §20.3 — e.g.
// all-zero durations, unresolved image size). Both entities carry UiCanvas + UiInstance +
// SplashTimer and are despawned automatically once SplashTimer.total_duration elapses (they
// always cross that threshold on the same frame, since both start at elapsed = 0 with the same
// total_duration) — the caller does not need to do anything else. The returned entity is the
// backdrop; it is the one to check with world.valid()/try_get<SplashTimer>() if a caller needs to
// inspect the splash's state.
[[nodiscard]] std::optional<ecs::Entity> show_splash(
        ecs::World& world, const SplashScreen& config, glm::vec2 image_size, WindowId window = kPrimaryWindow);

}
