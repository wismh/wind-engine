#pragma once

#include "editor_selection.h"
#include "world_view_view_model.h"

#include <engine/core/input_system.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/commands.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace engine {
class AssetsDb;
}

namespace editor {

// The World tab: loads the `.world` asset selected in the Project tab into an Edit Mode ECS world with
// UnknownComponentMode::Preserve and renders it into the scissored viewport area under the panel's 36 px header bar.
// Holds its own Editor Camera (2D pan/zoom and 3D orbit/pan/fly) so navigating in the editor does not mutate the
// scene's camera entity. Before the game's EditModule unloads (`before_module_unload`), the panel resets its edit
// world so no game-module component pool outlives the DLL; `reload` re-opens the `.world` once the module is back.
class WorldViewPanel {
public:
    // Height of `.world-header` in assets/css/panels.css. The scissored viewport sits below it.
    static constexpr float kHeaderHeight = 36.0f;
    static constexpr glm::vec4 kViewportClearColor{0.10f, 0.11f, 0.13f, 1.0f};

    explicit WorldViewPanel(const EditorSelection& selection);

    WorldViewPanel(const WorldViewPanel&) = delete;
    WorldViewPanel& operator=(const WorldViewPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<WorldViewViewModel>& view_model() const;

    void set_assets(engine::AssetsDb* assets);
    void set_component_registrar(std::function<void(engine::ecs::WorldSerializer&)> registrar);

    // Loads `path` into `edit_world_` and initializes the Editor Camera from the scene's ActiveCamera (or a default
    // 2D orthographic camera when the scene has none).
    bool open_world(const std::filesystem::path& path, std::string_view display_name = {});

    // Destroys `edit_world_` before `EditModule::unload` frees the game DLL, keeping `open_path_` for `reload()`.
    void before_module_unload();
    // Re-opens `open_path_` when set (after `EditModule` loads or reloads).
    void reload();
    // Clears `open_path_` and resets `edit_world_` (when opening another project or quitting).
    void close();

    // Checks whether a `.world` file was newly selected in `EditorSelection`. When so, opens it and returns true so
    // `EditorPanels` activates the World tab.
    bool sync_selection();

    void toggle_projection();
    void reset_camera();

    // Processes Editor Camera mouse/keyboard navigation when `visible` and a world is loaded. Always advances the
    // panel's event cursors so input from other tabs is never replayed later.
    void handle_input(
            engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, float dt, bool visible = true);
    void frame(engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, bool visible = true);

    // Appends the scissored world viewport draws of `edit_world_` to the command buffer of `world_canvas`'s window.
    void render(
            engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, const engine::EngineSystemDeps& deps);

    [[nodiscard]] bool world_loaded() const noexcept { return world_loaded_; }
    [[nodiscard]] const std::filesystem::path& open_path() const noexcept { return open_path_; }
    [[nodiscard]] engine::ecs::World& edit_world() noexcept { return edit_world_; }
    [[nodiscard]] const engine::ecs::World& edit_world() const noexcept { return edit_world_; }

    [[nodiscard]] const engine::ecs::Camera& camera() const noexcept { return camera_; }
    [[nodiscard]] glm::vec3 camera_position() const noexcept { return position_; }
    [[nodiscard]] glm::vec3 camera_euler_xyz() const noexcept { return euler_xyz_; }
    [[nodiscard]] glm::mat4 camera_world_matrix() const;

private:
    void set_default_camera();
    void init_camera_from_scene();
    void update_projection_label();
    [[nodiscard]] static bool is_top_canvas_at(
            engine::ecs::World& world, engine::WindowId window, engine::ecs::Entity target, glm::vec2 point);

    const EditorSelection* selection_;
    std::shared_ptr<WorldViewViewModel> view_model_;
    engine::ecs::World edit_world_;
    std::filesystem::path open_path_;
    std::string open_display_name_;
    bool world_loaded_ = false;
    engine::AssetsDb* assets_ = nullptr;
    std::function<void(engine::ecs::WorldSerializer&)> register_components_;
    std::optional<std::uint64_t> selection_revision_;

    engine::ecs::Camera camera_{};
    glm::vec3 position_{0.0f, 0.0f, 10.0f};
    glm::vec3 euler_xyz_{0.0f, 0.0f, 0.0f};

    engine::ecs::Camera initial_camera_{};
    glm::vec3 initial_position_{0.0f, 0.0f, 10.0f};
    glm::vec3 initial_euler_xyz_{0.0f, 0.0f, 0.0f};

    engine::ecs::EventCursor<engine::MouseEvent> mouse_cursor_;
    engine::ecs::EventCursor<engine::KeyEvent> key_cursor_;
    engine::MouseButton drag_button_ = engine::MouseButton::None;
    engine::WindowId drag_window_{};
    glm::vec2 last_drag_pos_{};
    bool held_w_ = false;
    bool held_s_ = false;
    bool held_a_ = false;
    bool held_d_ = false;
    bool held_q_ = false;
    bool held_e_ = false;
};

}
