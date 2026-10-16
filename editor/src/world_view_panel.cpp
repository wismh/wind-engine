#include "world_view_panel.h"

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>
#include <variant>

namespace editor {
namespace {

constexpr char kNoWorldOpen[] = "No world open.";
constexpr char kSelectWorldHint[] = "Select a .world file in the Project tab.";
constexpr char kOpaqueViewportBg[] = "#1e1f22";
constexpr char kTransparentViewportBg[] = "#00000000";

constexpr float kMinOrthoSize = 0.1f;
constexpr float kMaxOrthoSize = 10000.0f;
constexpr float kWheelZoomBase = 0.9f;
constexpr float kOrbitSensitivity = 0.005f;
constexpr float kMaxPitchRadians = 1.55f;
constexpr float kPan3DSensitivity = 0.02f;
constexpr float kWheel3DStep = 1.5f;
constexpr float kFlySpeed = 10.0f;

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Inverse of `engine::euler_xyz` (Rx(x) * Ry(y) * Rz(z)).
glm::vec3 extract_euler_xyz(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(glm::normalize(q));
    const float sin_y = std::clamp(m[2][0], -1.0f, 1.0f);
    const float y = std::asin(sin_y);
    if (std::abs(sin_y) < 0.99999f) {
        const float x = std::atan2(-m[2][1], m[2][2]);
        const float z = std::atan2(-m[1][0], m[0][0]);
        return {x, y, z};
    }
    return {std::atan2(m[1][2], m[1][1]), y, 0.0f};
}

glm::quat world_rotation_of(const engine::ecs::World& world, engine::ecs::Entity entity) {
    const glm::mat4 m = engine::ecs::world_matrix(world, entity);
    glm::vec3 col0{m[0]};
    glm::vec3 col1{m[1]};
    glm::vec3 col2{m[2]};
    const float len0 = glm::length(col0);
    const float len1 = glm::length(col1);
    const float len2 = glm::length(col2);
    if (len0 > 1e-6f) {
        col0 /= len0;
    }
    if (len1 > 1e-6f) {
        col1 /= len1;
    }
    if (len2 > 1e-6f) {
        col2 /= len2;
    }
    return glm::quat_cast(glm::mat3{col0, col1, col2});
}

engine::render::Rect viewport_rect_of(const engine::render::Rect& canvas_rect) {
    return engine::render::Rect{
            .x = canvas_rect.x,
            .y = canvas_rect.y + WorldViewPanel::kHeaderHeight,
            .w = canvas_rect.w,
            .h = std::max(0.0f, canvas_rect.h - WorldViewPanel::kHeaderHeight),
    };
}

}

WorldViewPanel::WorldViewPanel(const EditorSelection& selection)
    : selection_(&selection)
    , view_model_(std::make_shared<WorldViewViewModel>()) {
    view_model_->toggleProjection.bind_to<WorldViewPanel, &WorldViewPanel::toggle_projection>(*this);
    view_model_->resetCamera.bind_to<WorldViewPanel, &WorldViewPanel::reset_camera>(*this);
    close();
}

const std::shared_ptr<WorldViewViewModel>& WorldViewPanel::view_model() const {
    return view_model_;
}

void WorldViewPanel::set_assets(engine::AssetsDb* assets) {
    assets_ = assets;
}

void WorldViewPanel::set_component_registrar(std::function<void(engine::ecs::WorldSerializer&)> registrar) {
    register_components_ = std::move(registrar);
}

bool WorldViewPanel::open_world(const std::filesystem::path& path, std::string_view display_name) {
    open_path_ = path;
    open_display_name_ = display_name.empty() ? path_text(path.filename()) : std::string(display_name);
    edit_world_ = engine::ecs::World{};
    if (register_components_) {
        register_components_(edit_world_.ctx<engine::ecs::WorldSerializer>());
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        world_loaded_ = false;
        view_model_->viewportBg = std::string(kOpaqueViewportBg);
        view_model_->statusText = open_display_name_;
        view_model_->emptyText = "Failed to read " + path_text(path.filename());
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string toml_text = std::move(buffer).str();

    const auto loaded = engine::ecs::load_world_from_toml(
            edit_world_, toml_text, assets_, nullptr, engine::ecs::UnknownComponentMode::Preserve);
    if (!loaded) {
        world_loaded_ = false;
        view_model_->viewportBg = std::string(kOpaqueViewportBg);
        view_model_->statusText = open_display_name_;
        view_model_->emptyText = loaded.error().message;
        return false;
    }

    world_loaded_ = true;
    view_model_->viewportBg = std::string(kTransparentViewportBg);
    view_model_->emptyText = std::string{};
    const std::size_t count = loaded->entities.size();
    view_model_->statusText =
            std::format("{}  ({} {})", open_display_name_, count, count == 1 ? "entity" : "entities");

    init_camera_from_scene();
    return true;
}

void WorldViewPanel::before_module_unload() {
    edit_world_ = engine::ecs::World{};
}

void WorldViewPanel::reload() {
    if (!open_path_.empty()) {
        (void)open_world(open_path_, open_display_name_);
    }
}

void WorldViewPanel::close() {
    open_path_.clear();
    open_display_name_.clear();
    edit_world_ = engine::ecs::World{};
    world_loaded_ = false;
    set_default_camera();
    initial_camera_ = camera_;
    initial_position_ = position_;
    initial_euler_xyz_ = euler_xyz_;
    update_projection_label();
    view_model_->statusText = std::string(kNoWorldOpen);
    view_model_->viewportBg = std::string(kOpaqueViewportBg);
    view_model_->emptyText = std::string(kSelectWorldHint);
}

bool WorldViewPanel::sync_selection() {
    const SelectionTarget& target = selection_->target();
    if (selection_revision_ == selection_->revision()) {
        return false;
    }
    selection_revision_ = selection_->revision();
    const auto* asset = std::get_if<AssetSelection>(&target);
    if (asset == nullptr || asset->directory || asset->path.extension() != ".world") {
        return false;
    }
    (void)open_world(asset->path, asset->key);
    return true;
}

void WorldViewPanel::toggle_projection() {
    if (camera_.projection == engine::Projection::Orthographic) {
        camera_.projection = engine::Projection::Perspective;
        if (camera_.near_clip <= 0.0f) {
            camera_.near_clip = 0.1f;
        }
        if (camera_.far_clip <= camera_.near_clip) {
            camera_.far_clip = 1000.0f;
        }
    } else {
        camera_.projection = engine::Projection::Orthographic;
        euler_xyz_.x = 0.0f;
        euler_xyz_.y = 0.0f;
        if (camera_.near_clip > 0.0f) {
            camera_.near_clip = -100.0f;
        }
        if (camera_.far_clip <= camera_.near_clip) {
            camera_.far_clip = 100.0f;
        }
    }
    update_projection_label();
}

void WorldViewPanel::reset_camera() {
    camera_ = initial_camera_;
    position_ = initial_position_;
    euler_xyz_ = initial_euler_xyz_;
    update_projection_label();
}

void WorldViewPanel::frame(
        engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, bool visible) {
    const float dt = editor_world.ctx<engine::Time>().delta_time;
    handle_input(editor_world, world_canvas, dt, visible);
}

void WorldViewPanel::handle_input(
        engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, float dt, bool visible) {
    engine::ecs::EventReader<engine::MouseEvent> mouse_events{editor_world, mouse_cursor_};
    engine::ecs::EventReader<engine::KeyEvent> key_events{editor_world, key_cursor_};

    const engine::ui::UiCanvas* canvas = editor_world.try_get<engine::ui::UiCanvas>(world_canvas);
    if (!visible || !world_loaded_ || canvas == nullptr) {
        drag_button_ = engine::MouseButton::None;
        held_w_ = held_s_ = held_a_ = held_d_ = held_q_ = held_e_ = false;
        return;
    }

    const engine::render::Rect viewport = viewport_rect_of(canvas->rect);
    if (viewport.w <= 0.0f || viewport.h <= 0.0f) {
        drag_button_ = engine::MouseButton::None;
        return;
    }

    for (const engine::KeyEvent& event : key_events) {
        if (event.window != canvas->window) {
            continue;
        }
        switch (event.key) {
            case engine::KeyCode::W:
                held_w_ = event.down;
                break;
            case engine::KeyCode::S:
                held_s_ = event.down;
                break;
            case engine::KeyCode::A:
                held_a_ = event.down;
                break;
            case engine::KeyCode::D:
                held_d_ = event.down;
                break;
            case engine::KeyCode::Q:
                held_q_ = event.down;
                break;
            case engine::KeyCode::E:
                held_e_ = event.down;
                break;
            default:
                break;
        }
    }

    for (const engine::MouseEvent& event : mouse_events) {
        if (event.kind == engine::MouseEvent::Kind::Down) {
            if (event.window == canvas->window &&
                    engine::ui::rect_contains(viewport, event.position.x, event.position.y) &&
                    is_top_canvas_at(editor_world, event.window, world_canvas, event.position)) {
                drag_button_ = event.button;
                drag_window_ = event.window;
                last_drag_pos_ = event.position;
            }
        } else if (event.kind == engine::MouseEvent::Kind::Up) {
            if (event.button == drag_button_ || event.button == engine::MouseButton::None) {
                drag_button_ = engine::MouseButton::None;
            }
        } else if (event.kind == engine::MouseEvent::Kind::Move) {
            if (drag_button_ == engine::MouseButton::None || event.window != drag_window_) {
                continue;
            }
            const glm::vec2 delta = (event.relative.x != 0.0f || event.relative.y != 0.0f)
                    ? event.relative
                    : (event.position - last_drag_pos_);
            last_drag_pos_ = event.position;

            if (camera_.projection == engine::Projection::Orthographic) {
                const float world_per_pixel = (2.0f * camera_.ortho_size) / viewport.h;
                position_.x -= delta.x * world_per_pixel;
                position_.y += delta.y * world_per_pixel;
            } else {
                if (drag_button_ == engine::MouseButton::Right) {
                    euler_xyz_.y -= delta.x * kOrbitSensitivity;
                    euler_xyz_.x = std::clamp(
                            euler_xyz_.x - delta.y * kOrbitSensitivity, -kMaxPitchRadians, kMaxPitchRadians);
                } else {
                    const glm::quat rot = engine::euler_xyz(euler_xyz_);
                    const glm::vec3 right = rot * glm::vec3{1.0f, 0.0f, 0.0f};
                    const glm::vec3 up = rot * glm::vec3{0.0f, 1.0f, 0.0f};
                    position_ -= right * (delta.x * kPan3DSensitivity);
                    position_ += up * (delta.y * kPan3DSensitivity);
                }
            }
        } else if (event.kind == engine::MouseEvent::Kind::Wheel) {
            if (event.wheel_y == 0.0f || event.window != canvas->window ||
                    !engine::ui::rect_contains(viewport, event.position.x, event.position.y) ||
                    !is_top_canvas_at(editor_world, event.window, world_canvas, event.position)) {
                continue;
            }
            if (camera_.projection == engine::Projection::Orthographic) {
                camera_.ortho_size = std::clamp(
                        camera_.ortho_size * std::pow(kWheelZoomBase, event.wheel_y), kMinOrthoSize, kMaxOrthoSize);
            } else {
                const glm::vec3 forward = engine::euler_xyz(euler_xyz_) * glm::vec3{0.0f, 0.0f, -1.0f};
                position_ += forward * (event.wheel_y * kWheel3DStep);
            }
        }
    }

    if (camera_.projection == engine::Projection::Perspective && dt > 0.0f &&
            (held_w_ || held_s_ || held_a_ || held_d_ || held_q_ || held_e_)) {
        const glm::vec2 pointer = engine::ui::pointer_for(editor_world, canvas->window).position;
        const bool pointer_in_viewport = engine::ui::rect_contains(viewport, pointer.x, pointer.y) &&
                is_top_canvas_at(editor_world, canvas->window, world_canvas, pointer);
        if (drag_button_ == engine::MouseButton::Right || pointer_in_viewport) {
            const glm::quat rot = engine::euler_xyz(euler_xyz_);
            const glm::vec3 right = rot * glm::vec3{1.0f, 0.0f, 0.0f};
            const glm::vec3 up = rot * glm::vec3{0.0f, 1.0f, 0.0f};
            const glm::vec3 forward = rot * glm::vec3{0.0f, 0.0f, -1.0f};
            glm::vec3 move{0.0f};
            if (held_w_) {
                move += forward;
            }
            if (held_s_) {
                move -= forward;
            }
            if (held_d_) {
                move += right;
            }
            if (held_a_) {
                move -= right;
            }
            if (held_e_) {
                move += up;
            }
            if (held_q_) {
                move -= up;
            }
            position_ += move * (kFlySpeed * dt);
        }
    }
}

void WorldViewPanel::render(
        engine::ecs::World& editor_world, engine::ecs::Entity world_canvas, const engine::EngineSystemDeps& deps) {
    if (!world_loaded_) {
        return;
    }
    const engine::ui::UiCanvas* canvas = editor_world.try_get<engine::ui::UiCanvas>(world_canvas);
    if (canvas == nullptr || canvas->rect.w <= 0.0f || canvas->rect.h <= kHeaderHeight) {
        return;
    }
    engine::render::CommandBuffer* buffer =
            deps.commands_for_window ? deps.commands_for_window(canvas->window) : deps.commands;
    if (buffer == nullptr) {
        return;
    }
    engine::render_world_viewport(edit_world_, engine::WorldViewportRender{
            .commands = buffer,
            .viewport = viewport_rect_of(canvas->rect),
            .clear_color = kViewportClearColor,
            .camera = camera_,
            .camera_world = camera_world_matrix(),
            .assets = assets_ != nullptr ? assets_ : deps.assets,
            .fatal = nullptr,
    });
}

glm::mat4 WorldViewPanel::camera_world_matrix() const {
    return engine::model_matrix(engine::Transform{
            .position = position_,
            .rotation = engine::euler_xyz(euler_xyz_),
    });
}

void WorldViewPanel::set_default_camera() {
    camera_ = engine::Camera{
            .projection = engine::Projection::Orthographic,
            .ortho_size = 10.0f,
            .near_clip = -100.0f,
            .far_clip = 100.0f,
    };
    position_ = {0.0f, 0.0f, 10.0f};
    euler_xyz_ = {0.0f, 0.0f, 0.0f};
}

void WorldViewPanel::init_camera_from_scene() {
    set_default_camera();
    const engine::ActiveCamera& active = edit_world_.ctx<engine::ActiveCamera>();
    if (edit_world_.valid(active.entity)) {
        const engine::Camera* scene_cam = edit_world_.try_get<engine::Camera>(active.entity);
        const engine::Transform* scene_tr = edit_world_.try_get<engine::Transform>(active.entity);
        if (scene_cam != nullptr && scene_tr != nullptr) {
            camera_ = *scene_cam;
            position_ = engine::ecs::world_position(edit_world_, active.entity);
            euler_xyz_ = extract_euler_xyz(world_rotation_of(edit_world_, active.entity));
        }
    }
    initial_camera_ = camera_;
    initial_position_ = position_;
    initial_euler_xyz_ = euler_xyz_;
    update_projection_label();
}

void WorldViewPanel::update_projection_label() {
    view_model_->projectionLabel =
            camera_.projection == engine::Projection::Perspective ? std::string("3D") : std::string("2D");
}

bool WorldViewPanel::is_top_canvas_at(
        engine::ecs::World& world, engine::WindowId window, engine::ecs::Entity target, glm::vec2 point) {
    engine::ecs::Entity top{};
    std::optional<int> top_order;
    auto view = world.view<engine::ui::UiCanvas>();
    for (const engine::ecs::Entity entity : view) {
        const engine::ui::UiCanvas& canvas = view.get<engine::ui::UiCanvas>(entity);
        if (canvas.window != window || !engine::ui::rect_contains(canvas.rect, point.x, point.y)) {
            continue;
        }
        if (!top_order || canvas.order > *top_order) {
            top = entity;
            top_order = canvas.order;
        }
    }
    return top == target;
}

}
