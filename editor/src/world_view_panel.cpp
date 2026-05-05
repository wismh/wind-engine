#include "world_view_panel.h"

#include <engine/builtin_ids.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/primitives.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

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

bool test_axis_segment(
        const engine::Ray& ray,
        glm::vec3 p0,
        glm::vec3 u,
        float length,
        float threshold,
        float& out_ray_t) {
    const glm::vec3 r0 = ray.origin;
    const glm::vec3 r = glm::normalize(ray.direction);
    const glm::vec3 w = p0 - r0;

    const float b = glm::dot(u, r);
    const float wu = glm::dot(u, w);
    const float wr = glm::dot(r, w);

    const float denom = 1.0f - b * b;
    float s = 0.0f;
    float t = 0.0f;

    if (denom > 1e-4f) {
        s = std::clamp((b * wr - wu) / denom, 0.0f, length);
        t = b * s + wr;
    } else {
        s = 0.0f;
        t = wr;
    }

    t = std::max(0.0f, t);
    const glm::vec3 pt_seg = p0 + s * u;
    const glm::vec3 pt_ray = r0 + t * r;
    const float dist = glm::distance(pt_seg, pt_ray);
    if (dist <= threshold) {
        out_ray_t = t;
        return true;
    }
    return false;
}

bool test_center_sphere(
        const engine::Ray& ray,
        glm::vec3 p0,
        float radius,
        float& out_ray_t) {
    const glm::vec3 r0 = ray.origin;
    const glm::vec3 r = glm::normalize(ray.direction);
    const glm::vec3 w = p0 - r0;
    const float t = glm::dot(w, r);
    const glm::vec3 pt_ray = r0 + std::max(0.0f, t) * r;
    const float dist = glm::distance(p0, pt_ray);
    if (dist <= radius) {
        out_ray_t = std::max(0.0f, t);
        return true;
    }
    return false;
}

bool test_rotation_ring(
        const engine::Ray& ray,
        glm::vec3 center,
        glm::vec3 normal,
        float radius,
        float threshold,
        float& out_ray_t) {
    const float denom = glm::dot(ray.direction, normal);
    if (std::abs(denom) < 1e-4f) {
        return false;
    }
    const float t = glm::dot(center - ray.origin, normal) / denom;
    if (t < 0.0f) {
        return false;
    }
    const glm::vec3 hit_pt = ray.origin + t * ray.direction;
    const float dist_from_center = glm::distance(hit_pt, center);
    if (std::abs(dist_from_center - radius) <= threshold) {
        out_ray_t = t;
        return true;
    }
    return false;
}

float compute_rotation_angle(
        glm::vec2 pointer_local,
        GizmoAxis axis,
        const engine::render::Rect& viewport,
        const engine::ecs::Camera& camera,
        const glm::mat4& cam_world,
        glm::vec3 world_pos) {
    const engine::ui::WindowSize vp_size{
            std::max(1, static_cast<int>(std::round(viewport.w))),
            std::max(1, static_cast<int>(std::round(viewport.h))),
    };
    const engine::Ray ray = engine::screen_to_ray(pointer_local, camera, cam_world, vp_size);
    glm::vec3 n{0.0f};
    glm::vec3 u{0.0f};
    glm::vec3 v{0.0f};
    switch (axis) {
        case GizmoAxis::X:
            n = glm::vec3{1.0f, 0.0f, 0.0f};
            u = glm::vec3{0.0f, 1.0f, 0.0f};
            v = glm::vec3{0.0f, 0.0f, 1.0f};
            break;
        case GizmoAxis::Y:
            n = glm::vec3{0.0f, 1.0f, 0.0f};
            u = glm::vec3{0.0f, 0.0f, 1.0f};
            v = glm::vec3{1.0f, 0.0f, 0.0f};
            break;
        case GizmoAxis::Z:
        default:
            n = glm::vec3{0.0f, 0.0f, 1.0f};
            u = glm::vec3{1.0f, 0.0f, 0.0f};
            v = glm::vec3{0.0f, 1.0f, 0.0f};
            break;
    }
    const float denom = glm::dot(ray.direction, n);
    if (std::abs(denom) > 1e-5f) {
        const float t = glm::dot(world_pos - ray.origin, n) / denom;
        const glm::vec3 hit_pt = ray.origin + t * ray.direction;
        const glm::vec3 offset = hit_pt - world_pos;
        return std::atan2(glm::dot(offset, v), glm::dot(offset, u));
    }
    return 0.0f;
}

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Inverse of `camera_rotation()` (Ry(yaw) * Rx(pitch)).
glm::vec3 extract_pitch_yaw(const glm::quat& q) {
    const glm::quat norm_q = glm::normalize(q);
    const glm::vec3 f = norm_q * glm::vec3{0.0f, 0.0f, -1.0f};
    const float sin_pitch = std::clamp(f.y, -1.0f, 1.0f);
    const float pitch = std::asin(sin_pitch);
    float yaw = 0.0f;
    if (std::abs(f.y) < 0.9999f) {
        yaw = std::atan2(-f.x, -f.z);
    } else {
        const glm::vec3 r = norm_q * glm::vec3{1.0f, 0.0f, 0.0f};
        yaw = std::atan2(-r.z, r.x);
    }
    return {pitch, yaw, 0.0f};
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
    view_model_->setToolTranslate.bind_to<WorldViewPanel, &WorldViewPanel::set_tool_translate>(*this);
    view_model_->setToolRotate.bind_to<WorldViewPanel, &WorldViewPanel::set_tool_rotate>(*this);
    view_model_->setToolScale.bind_to<WorldViewPanel, &WorldViewPanel::set_tool_scale>(*this);
    update_tool_labels();
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
    is_dirty_ = false;
    held_ctrl_ = false;
    view_model_->viewportBg = std::string(kTransparentViewportBg);
    view_model_->emptyText = std::string{};
    const std::size_t count = loaded->entities.size();
    view_model_->statusText =
            std::format("{}  ({} {})", open_display_name_, count, count == 1 ? "entity" : "entities");

    init_camera_from_scene();
    return true;
}

bool WorldViewPanel::save_world() {
    if (!world_loaded_ || open_path_.empty()) {
        return false;
    }
    const std::string toml_text = engine::ecs::save_world_to_toml(edit_world_, nullptr);
    const std::filesystem::path temp_path = open_path_.string() + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << toml_text;
        if (!out.good()) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp_path, open_path_, ec);
    if (ec) {
        std::ofstream out(open_path_, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << toml_text;
        if (!out.good()) {
            return false;
        }
    }
    is_dirty_ = false;
    return true;
}

void WorldViewPanel::before_module_unload() {
    edit_world_ = engine::ecs::World{};
    gizmo_hovered_ = GizmoAxis::None;
    gizmo_drag_axis_ = GizmoAxis::None;
    gizmo_visible_ = false;
    is_dirty_ = false;
    held_ctrl_ = false;
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
    is_dirty_ = false;
    held_ctrl_ = false;
    gizmo_mode_ = GizmoMode::Translate;
    gizmo_hovered_ = GizmoAxis::None;
    gizmo_drag_axis_ = GizmoAxis::None;
    gizmo_visible_ = false;
    set_default_camera();
    initial_camera_ = camera_;
    initial_position_ = position_;
    initial_euler_xyz_ = euler_xyz_;
    update_projection_label();
    update_tool_labels();
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

void WorldViewPanel::set_gizmo_mode(GizmoMode mode) {
    gizmo_mode_ = mode;
    gizmo_hovered_ = GizmoAxis::None;
    gizmo_drag_axis_ = GizmoAxis::None;
    update_tool_labels();
}

void WorldViewPanel::set_tool_translate() {
    set_gizmo_mode(GizmoMode::Translate);
}

void WorldViewPanel::set_tool_rotate() {
    set_gizmo_mode(GizmoMode::Rotate);
}

void WorldViewPanel::set_tool_scale() {
    set_gizmo_mode(GizmoMode::Scale);
}

void WorldViewPanel::update_tool_labels() {
    view_model_->moveBtnLabel = gizmo_mode_ == GizmoMode::Translate ? std::string("[ Move ]") : std::string("Move");
    view_model_->rotateBtnLabel = gizmo_mode_ == GizmoMode::Rotate ? std::string("[ Rotate ]") : std::string("Rotate");
    view_model_->scaleBtnLabel = gizmo_mode_ == GizmoMode::Scale ? std::string("[ Scale ]") : std::string("Scale");
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
        if (drag_button_ != engine::MouseButton::None && drag_window_ != engine::WindowId{}) {
            engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] = engine::ui::Cursor::Default;
        }
        drag_button_ = engine::MouseButton::None;
        held_w_ = held_s_ = held_a_ = held_d_ = held_q_ = held_e_ = held_ctrl_ = false;
        gizmo_hovered_ = GizmoAxis::None;
        gizmo_drag_axis_ = GizmoAxis::None;
        gizmo_visible_ = false;
        return;
    }

    const engine::render::Rect viewport = viewport_rect_of(canvas->rect);
    if (viewport.w <= 0.0f || viewport.h <= 0.0f) {
        if (drag_button_ != engine::MouseButton::None && drag_window_ != engine::WindowId{}) {
            engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] = engine::ui::Cursor::Default;
        }
        drag_button_ = engine::MouseButton::None;
        held_w_ = held_s_ = held_a_ = held_d_ = held_q_ = held_e_ = held_ctrl_ = false;
        gizmo_hovered_ = GizmoAxis::None;
        gizmo_drag_axis_ = GizmoAxis::None;
        gizmo_visible_ = false;
        return;
    }

    for (const engine::KeyEvent& event : key_events) {
        if (event.window != canvas->window) {
            continue;
        }
        switch (event.key) {
            case engine::KeyCode::LCtrl:
            case engine::KeyCode::RCtrl:
                held_ctrl_ = event.down;
                break;
            case engine::KeyCode::S:
                if (held_ctrl_) {
                    if (event.down) {
                        (void)save_world();
                    }
                } else {
                    held_s_ = event.down;
                }
                break;
            case engine::KeyCode::W:
                if (!held_ctrl_) {
                    held_w_ = event.down;
                    if (event.down && drag_button_ == engine::MouseButton::None) {
                        set_tool_translate();
                    }
                }
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
                if (!held_ctrl_) {
                    held_e_ = event.down;
                    if (event.down && drag_button_ == engine::MouseButton::None) {
                        set_tool_rotate();
                    }
                }
                break;
            case engine::KeyCode::R:
                if (!held_ctrl_) {
                    if (event.down && drag_button_ == engine::MouseButton::None) {
                        set_tool_scale();
                    }
                }
                break;
            default:
                break;
        }
    }

    if (drag_button_ == engine::MouseButton::None) {
        const glm::vec2 current_pointer = engine::ui::pointer_for(editor_world, canvas->window).position;
        update_gizmo(viewport, current_pointer);
    }

    for (const engine::MouseEvent& event : mouse_events) {
        if (event.kind == engine::MouseEvent::Kind::Down) {
            if (event.window == canvas->window &&
                    engine::ui::rect_contains(viewport, event.position.x, event.position.y) &&
                    is_top_canvas_at(editor_world, event.window, world_canvas, event.position)) {
                if (event.button == engine::MouseButton::Left && gizmo_hovered_ != GizmoAxis::None) {
                    const auto selected = resolve_selected_entity();
                    if (selected) {
                        gizmo_drag_axis_ = gizmo_hovered_;
                        gizmo_drag_start_world_pos_ = engine::ecs::world_position(edit_world_, *selected);
                        const auto* tr = edit_world_.try_get<engine::Transform>(*selected);
                        if (tr != nullptr) {
                            gizmo_drag_start_rot_ = tr->rotation;
                            gizmo_drag_start_scale_ = tr->scale;
                        }
                        const glm::vec2 pointer_local = event.position - glm::vec2{viewport.x, viewport.y};
                        gizmo_drag_start_screen_pos_ = pointer_local;
                        gizmo_drag_start_t_ = compute_axis_param(pointer_local, gizmo_drag_axis_, viewport);
                        if (gizmo_mode_ == GizmoMode::Rotate) {
                            gizmo_drag_start_angle_ = compute_rotation_angle(
                                    pointer_local, gizmo_drag_axis_, viewport, camera_, camera_world_matrix(),
                                    gizmo_drag_start_world_pos_);
                        }
                        drag_button_ = event.button;
                        drag_window_ = event.window;
                        last_drag_pos_ = event.position;
                        engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] =
                                engine::ui::Cursor::Move;
                        update_gizmo(viewport, event.position);
                        continue;
                    }
                }
                gizmo_drag_axis_ = GizmoAxis::None;
                drag_button_ = event.button;
                drag_window_ = event.window;
                last_drag_pos_ = event.position;
                engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] = engine::ui::Cursor::Move;
            }
        } else if (event.kind == engine::MouseEvent::Kind::Up) {
            if (event.button == drag_button_ || event.button == engine::MouseButton::None) {
                if (drag_button_ != engine::MouseButton::None && drag_window_ != engine::WindowId{}) {
                    engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] =
                            engine::ui::Cursor::Default;
                }
                drag_button_ = engine::MouseButton::None;
                gizmo_drag_axis_ = GizmoAxis::None;
                update_gizmo(viewport, event.position);
            }
        } else if (event.kind == engine::MouseEvent::Kind::Move) {
            if (drag_button_ == engine::MouseButton::None || event.window != drag_window_) {
                continue;
            }
            engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] = engine::ui::Cursor::Move;
            const glm::vec2 delta = (event.relative.x != 0.0f || event.relative.y != 0.0f)
                    ? event.relative
                    : (event.position - last_drag_pos_);
            last_drag_pos_ = event.position;

            if (gizmo_drag_axis_ != GizmoAxis::None) {
                apply_gizmo_drag(viewport, event.position);
                update_gizmo(viewport, event.position);
                continue;
            }

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
                    const glm::quat rot = camera_rotation();
                    const glm::vec3 right = rot * glm::vec3{1.0f, 0.0f, 0.0f};
                    const glm::vec3 up = rot * glm::vec3{0.0f, 1.0f, 0.0f};
                    position_ -= right * (delta.x * kPan3DSensitivity);
                    position_ += up * (delta.y * kPan3DSensitivity);
                }
            }
            update_gizmo(viewport, event.position);
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
                const glm::vec3 forward = camera_rotation() * glm::vec3{0.0f, 0.0f, -1.0f};
                position_ += forward * (event.wheel_y * kWheel3DStep);
            }
            update_gizmo(viewport, event.position);
        }
    }

    if (drag_button_ != engine::MouseButton::None && drag_window_ != engine::WindowId{}) {
        engine::ui::presentation_of(editor_world).cursors.cursors[drag_window_] = engine::ui::Cursor::Move;
    }

    if (camera_.projection == engine::Projection::Perspective && dt > 0.0f &&
            (held_w_ || held_s_ || held_a_ || held_d_ || held_q_ || held_e_)) {
        const glm::vec2 pointer = engine::ui::pointer_for(editor_world, canvas->window).position;
        const bool pointer_in_viewport = engine::ui::rect_contains(viewport, pointer.x, pointer.y) &&
                is_top_canvas_at(editor_world, canvas->window, world_canvas, pointer);
        if (drag_button_ == engine::MouseButton::Right || pointer_in_viewport) {
            const glm::quat rot = camera_rotation();
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
            update_gizmo(viewport, pointer);
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
    const engine::render::Rect viewport = viewport_rect_of(canvas->rect);
    engine::AssetsDb* assets = assets_ != nullptr ? assets_ : deps.assets;
    engine::render_world_viewport(edit_world_, engine::WorldViewportRender{
            .commands = buffer,
            .viewport = viewport,
            .clear_color = kViewportClearColor,
            .camera = camera_,
            .camera_world = camera_world_matrix(),
            .assets = assets,
            .fatal = nullptr,
    });
    render_gizmo_and_outline(*buffer, viewport, assets);
}

glm::quat WorldViewPanel::camera_rotation() const {
    const glm::quat q_yaw = glm::angleAxis(euler_xyz_.y, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat q_pitch = glm::angleAxis(euler_xyz_.x, glm::vec3{1.0f, 0.0f, 0.0f});
    return q_yaw * q_pitch;
}

glm::mat4 WorldViewPanel::camera_world_matrix() const {
    return engine::model_matrix(engine::Transform{
            .position = position_,
            .rotation = camera_rotation(),
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
            euler_xyz_ = extract_pitch_yaw(world_rotation_of(edit_world_, active.entity));
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

std::optional<engine::ecs::Entity> WorldViewPanel::resolve_selected_entity() const {
    if (selection_ == nullptr) {
        return std::nullopt;
    }
    const auto* entity_sel = std::get_if<EntitySelection>(&selection_->target());
    if (entity_sel == nullptr) {
        return std::nullopt;
    }
    if (!edit_world_.valid(entity_sel->entity)) {
        return std::nullopt;
    }
    if (edit_world_.try_get<engine::Transform>(entity_sel->entity) == nullptr) {
        return std::nullopt;
    }
    return entity_sel->entity;
}

float WorldViewPanel::compute_gizmo_scale(glm::vec3 world_pos, const engine::render::Rect& viewport) const {
    const float vp_h = std::max(1.0f, viewport.h);
    constexpr float kScreenGizmoPixels = 65.0f;
    if (camera_.projection == engine::Projection::Orthographic) {
        const float visible_height = 2.0f * camera_.ortho_size;
        return kScreenGizmoPixels * (visible_height / vp_h);
    }
    const float dist = std::max(1e-4f, glm::distance(position_, world_pos));
    const float visible_height = 2.0f * dist * std::tan(camera_.fov_y * 0.5f);
    return kScreenGizmoPixels * (visible_height / vp_h);
}

void WorldViewPanel::update_gizmo(const engine::render::Rect& viewport, glm::vec2 pointer) {
    const auto selected = resolve_selected_entity();
    if (!selected) {
        gizmo_visible_ = false;
        gizmo_hovered_ = GizmoAxis::None;
        return;
    }

    const glm::mat4 cam_world = camera_world_matrix();
    const glm::vec3 world_pos = engine::ecs::world_position(edit_world_, *selected);

    if (camera_.projection == engine::Projection::Perspective) {
        const glm::mat4 view = engine::view_matrix(cam_world);
        const glm::vec4 view_pos = view * glm::vec4(world_pos, 1.0f);
        if (view_pos.z >= -std::max(camera_.near_clip, engine::kMinPerspectiveNear)) {
            gizmo_visible_ = false;
            gizmo_hovered_ = GizmoAxis::None;
            return;
        }
    }

    gizmo_visible_ = true;
    gizmo_world_pos_ = world_pos;
    gizmo_scale_ = compute_gizmo_scale(world_pos, viewport);
    gizmo_has_z_ = (camera_.projection == engine::Projection::Perspective);

    if (gizmo_drag_axis_ != GizmoAxis::None) {
        return;
    }

    const bool pointer_in_viewport = engine::ui::rect_contains(viewport, pointer.x, pointer.y);
    if (!pointer_in_viewport) {
        gizmo_hovered_ = GizmoAxis::None;
        return;
    }

    const engine::ui::WindowSize vp_size{
            std::max(1, static_cast<int>(std::round(viewport.w))),
            std::max(1, static_cast<int>(std::round(viewport.h))),
    };
    const glm::vec2 pointer_local = pointer - glm::vec2{viewport.x, viewport.y};
    const engine::Ray ray = engine::screen_to_ray(pointer_local, camera_, cam_world, vp_size);

    if (gizmo_mode_ == GizmoMode::Rotate) {
        struct RingCandidate {
            GizmoAxis axis;
            glm::vec3 normal;
        };
        std::vector<RingCandidate> candidates;
        if (gizmo_has_z_) {
            candidates.push_back({GizmoAxis::X, glm::vec3{1.0f, 0.0f, 0.0f}});
            candidates.push_back({GizmoAxis::Y, glm::vec3{0.0f, 1.0f, 0.0f}});
            candidates.push_back({GizmoAxis::Z, glm::vec3{0.0f, 0.0f, 1.0f}});
        } else {
            candidates.push_back({GizmoAxis::Z, glm::vec3{0.0f, 0.0f, 1.0f}});
        }

        GizmoAxis best_axis = GizmoAxis::None;
        float min_t = std::numeric_limits<float>::max();
        for (const auto& cand : candidates) {
            float ray_t = 0.0f;
            if (test_rotation_ring(ray, world_pos, cand.normal, gizmo_scale_, 0.15f * gizmo_scale_, ray_t)) {
                if (ray_t < min_t) {
                    min_t = ray_t;
                    best_axis = cand.axis;
                }
            }
        }
        gizmo_hovered_ = best_axis;
        return;
    }

    float t_center = 0.0f;
    if (test_center_sphere(ray, world_pos, 0.18f * gizmo_scale_, t_center)) {
        gizmo_hovered_ = GizmoAxis::Center;
        return;
    }

    struct AxisCandidate {
        GizmoAxis axis;
        glm::vec3 dir;
    };
    std::vector<AxisCandidate> candidates{
            {GizmoAxis::X, glm::vec3{1.0f, 0.0f, 0.0f}},
            {GizmoAxis::Y, glm::vec3{0.0f, 1.0f, 0.0f}},
    };
    if (gizmo_has_z_) {
        candidates.push_back({GizmoAxis::Z, glm::vec3{0.0f, 0.0f, 1.0f}});
    }

    GizmoAxis best_axis = GizmoAxis::None;
    float min_t = std::numeric_limits<float>::max();
    for (const auto& cand : candidates) {
        float ray_t = 0.0f;
        if (test_axis_segment(ray, world_pos, cand.dir, gizmo_scale_, 0.15f * gizmo_scale_, ray_t)) {
            if (ray_t < min_t) {
                min_t = ray_t;
                best_axis = cand.axis;
            }
        }
    }
    gizmo_hovered_ = best_axis;
}

void WorldViewPanel::apply_gizmo_drag(const engine::render::Rect& viewport, glm::vec2 pointer) {
    if (gizmo_drag_axis_ == GizmoAxis::None) {
        return;
    }
    const auto selected = resolve_selected_entity();
    if (!selected) {
        return;
    }

    const glm::vec2 pointer_local = pointer - glm::vec2{viewport.x, viewport.y};

    if (gizmo_mode_ == GizmoMode::Rotate) {
        auto* tr = edit_world_.try_get<engine::Transform>(*selected);
        if (tr == nullptr) {
            return;
        }
        const float current_angle = compute_rotation_angle(
                pointer_local, gizmo_drag_axis_, viewport, camera_, camera_world_matrix(),
                gizmo_drag_start_world_pos_);
        const float delta_angle = current_angle - gizmo_drag_start_angle_;
        glm::vec3 normal{0.0f};
        if (gizmo_drag_axis_ == GizmoAxis::X) {
            normal.x = 1.0f;
        } else if (gizmo_drag_axis_ == GizmoAxis::Y) {
            normal.y = 1.0f;
        } else if (gizmo_drag_axis_ == GizmoAxis::Z) {
            normal.z = 1.0f;
        }
        const glm::quat rot_delta = glm::angleAxis(delta_angle, normal);
        tr->rotation = glm::normalize(rot_delta * gizmo_drag_start_rot_);
        engine::ecs::update_world_transforms(edit_world_);
        is_dirty_ = true;
        return;
    }

    if (gizmo_mode_ == GizmoMode::Scale) {
        auto* tr = edit_world_.try_get<engine::Transform>(*selected);
        if (tr == nullptr) {
            return;
        }
        if (gizmo_drag_axis_ == GizmoAxis::Center) {
            const glm::vec2 delta_screen = pointer_local - gizmo_drag_start_screen_pos_;
            const float factor = std::max(0.01f, 1.0f + (delta_screen.x - delta_screen.y) * 0.01f);
            tr->scale = gizmo_drag_start_scale_ * factor;
        } else {
            const float current_t = compute_axis_param(pointer_local, gizmo_drag_axis_, viewport);
            const float delta_t = current_t - gizmo_drag_start_t_;
            const float factor = std::max(0.01f, 1.0f + delta_t / gizmo_scale_);
            glm::vec3 new_scale = gizmo_drag_start_scale_;
            if (gizmo_drag_axis_ == GizmoAxis::X) {
                new_scale.x = gizmo_drag_start_scale_.x * factor;
            } else if (gizmo_drag_axis_ == GizmoAxis::Y) {
                new_scale.y = gizmo_drag_start_scale_.y * factor;
            } else if (gizmo_drag_axis_ == GizmoAxis::Z) {
                new_scale.z = gizmo_drag_start_scale_.z * factor;
            }
            tr->scale = new_scale;
        }
        engine::ecs::update_world_transforms(edit_world_);
        is_dirty_ = true;
        return;
    }

    if (gizmo_drag_axis_ == GizmoAxis::Center) {
        if (camera_.projection == engine::Projection::Orthographic) {
            const float world_per_pixel = (2.0f * camera_.ortho_size) / viewport.h;
            const glm::vec2 delta_screen = pointer_local - gizmo_drag_start_screen_pos_;
            const glm::vec3 new_pos = gizmo_drag_start_world_pos_ +
                    glm::vec3{delta_screen.x * world_per_pixel, -delta_screen.y * world_per_pixel, 0.0f};
            engine::ecs::set_world_position(edit_world_, *selected, new_pos);
            engine::ecs::update_world_transforms(edit_world_);
            is_dirty_ = true;
        } else {
            const engine::ui::WindowSize vp_size{
                    std::max(1, static_cast<int>(std::round(viewport.w))),
                    std::max(1, static_cast<int>(std::round(viewport.h))),
            };
            const glm::mat4 cam_world = camera_world_matrix();
            const glm::vec3 cam_fwd = camera_rotation() * glm::vec3{0.0f, 0.0f, -1.0f};
            const engine::Ray ray_curr = engine::screen_to_ray(pointer_local, camera_, cam_world, vp_size);
            const engine::Ray ray_start =
                    engine::screen_to_ray(gizmo_drag_start_screen_pos_, camera_, cam_world, vp_size);

            const float ndotd_curr = glm::dot(cam_fwd, ray_curr.direction);
            const float ndotd_start = glm::dot(cam_fwd, ray_start.direction);
            if (std::abs(ndotd_curr) > 1e-5f && std::abs(ndotd_start) > 1e-5f) {
                const float s_curr = glm::dot(gizmo_drag_start_world_pos_ - ray_curr.origin, cam_fwd) / ndotd_curr;
                const float s_start = glm::dot(gizmo_drag_start_world_pos_ - ray_start.origin, cam_fwd) / ndotd_start;
                const glm::vec3 hit_curr = ray_curr.origin + s_curr * ray_curr.direction;
                const glm::vec3 hit_start = ray_start.origin + s_start * ray_start.direction;
                const glm::vec3 new_pos = gizmo_drag_start_world_pos_ + (hit_curr - hit_start);
                engine::ecs::set_world_position(edit_world_, *selected, new_pos);
                engine::ecs::update_world_transforms(edit_world_);
                is_dirty_ = true;
            }
        }
    } else {
        const float current_t = compute_axis_param(pointer_local, gizmo_drag_axis_, viewport);
        const float delta_t = current_t - gizmo_drag_start_t_;
        glm::vec3 u{0.0f};
        if (gizmo_drag_axis_ == GizmoAxis::X) {
            u.x = 1.0f;
        } else if (gizmo_drag_axis_ == GizmoAxis::Y) {
            u.y = 1.0f;
        } else if (gizmo_drag_axis_ == GizmoAxis::Z) {
            u.z = 1.0f;
        }
        const glm::vec3 new_pos = gizmo_drag_start_world_pos_ + u * delta_t;
        engine::ecs::set_world_position(edit_world_, *selected, new_pos);
        engine::ecs::update_world_transforms(edit_world_);
        is_dirty_ = true;
    }
}

float WorldViewPanel::compute_axis_param(
        glm::vec2 local_screen_pt, GizmoAxis axis, const engine::render::Rect& viewport) const {
    const glm::mat4 cam_world = camera_world_matrix();
    const engine::ui::WindowSize vp_size{
            std::max(1, static_cast<int>(std::round(viewport.w))),
            std::max(1, static_cast<int>(std::round(viewport.h))),
    };

    const engine::Ray ray = engine::screen_to_ray(local_screen_pt, camera_, cam_world, vp_size);

    glm::vec3 u{0.0f};
    switch (axis) {
        case GizmoAxis::X:
            u = glm::vec3{1.0f, 0.0f, 0.0f};
            break;
        case GizmoAxis::Y:
            u = glm::vec3{0.0f, 1.0f, 0.0f};
            break;
        case GizmoAxis::Z:
            u = glm::vec3{0.0f, 0.0f, 1.0f};
            break;
        default:
            return 0.0f;
    }

    const glm::vec3 p0 = gizmo_drag_start_world_pos_;
    const glm::vec3 r0 = ray.origin;
    const glm::vec3 r = glm::normalize(ray.direction);
    const glm::vec3 w0 = p0 - r0;

    const float b = glm::dot(u, r);
    const float d = glm::dot(u, w0);
    const float e = glm::dot(r, w0);

    const float denom = 1.0f - b * b;
    if (denom > 1e-4f) {
        return (b * e - d) / denom;
    }

    const glm::vec3 cam_fwd = camera_rotation() * glm::vec3{0.0f, 0.0f, -1.0f};
    const float ndotd = glm::dot(cam_fwd, r);
    if (std::abs(ndotd) > 1e-5f) {
        const float s = glm::dot(p0 - r0, cam_fwd) / ndotd;
        const glm::vec3 hit_pt = r0 + s * r;
        return glm::dot(hit_pt - p0, u);
    }
    return 0.0f;
}

void WorldViewPanel::ensure_gizmo_meshes(engine::render::IGraphicFactory& factory) {
    if (!gizmo_cylinder_) {
        gizmo_cylinder_ = factory.create_mesh(engine::render::make_cylinder(1.0f, 1.0f, 16));
    }
    if (!gizmo_cone_) {
        gizmo_cone_ = factory.create_mesh(engine::render::make_cone(1.0f, 1.0f, 16));
    }
    if (!gizmo_cube_) {
        gizmo_cube_ = factory.create_mesh(engine::render::make_cube(1.0f));
    }
    if (!gizmo_torus_) {
        gizmo_torus_ = factory.create_mesh(engine::render::make_torus(1.0f, 0.030f, 48, 8));
    }
}

void WorldViewPanel::render_gizmo_and_outline(
        engine::render::CommandBuffer& buffer, const engine::render::Rect& viewport, engine::AssetsDb* assets) {
    (void)viewport;
    if (!gizmo_visible_) {
        return;
    }
    const auto selected = resolve_selected_entity();
    if (!selected) {
        return;
    }

    if (assets != nullptr && assets->graphic_factory() != nullptr) {
        ensure_gizmo_meshes(*assets->graphic_factory());
    }
    if (!gizmo_cylinder_ || !gizmo_cone_ || !gizmo_cube_ || !gizmo_torus_) {
        return;
    }

    std::shared_ptr<engine::render::IMaterial> unlit_mat;
    if (assets != nullptr) {
        if (auto mat_res = assets->try_get<engine::render::IMaterial>(engine::builtin::material_unlit)) {
            unlit_mat = *mat_res;
        }
    }

    // 1. Draw Selection Outline / Bounding Box
    constexpr glm::vec4 kOutlineColor{0.95f, 0.55f, 0.15f, 1.0f};
    const float edge_thickness = 0.018f * gizmo_scale_;

    const auto* sprite = edit_world_.try_get<engine::render::Sprite>(*selected);
    const auto* renderable = edit_world_.try_get<engine::render::Renderable>(*selected);

    auto draw_edge = [&](glm::vec3 a, glm::vec3 b) {
        const glm::vec3 v = b - a;
        const float len = glm::length(v);
        if (len < 1e-4f) {
            return;
        }
        const glm::vec3 dir = v / len;
        const glm::vec3 mid = (a + b) * 0.5f;

        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const float dot = glm::dot(up, dir);
        if (dot < -0.9999f) {
            rot = glm::angleAxis(glm::pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        } else if (dot < 0.9999f) {
            const glm::vec3 axis = glm::normalize(glm::cross(up, dir));
            const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
            rot = glm::angleAxis(angle, axis);
        }

        const glm::mat4 model = glm::translate(glm::mat4(1.0f), mid) * glm::mat4_cast(rot) *
                glm::scale(glm::mat4(1.0f), glm::vec3(edge_thickness, len, edge_thickness));

        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_cube_,
                .material = unlit_mat,
                .model = model,
                .color = kOutlineColor,
        });
    };

    if (sprite != nullptr) {
        glm::mat4 sprite_model = engine::ecs::world_matrix(edit_world_, *selected);
        if (sprite->pixel_size.x > 0.0f && sprite->pixel_size.y > 0.0f && sprite->pixels_per_unit > 0.0f) {
            const glm::vec2 world_size = sprite->pixel_size / sprite->pixels_per_unit;
            const glm::vec3 pivot_offset{
                    (0.5f - sprite->pivot.x) * world_size.x, (0.5f - sprite->pivot.y) * world_size.y, 0.0f};
            sprite_model = glm::translate(sprite_model, pivot_offset);
            sprite_model = glm::scale(sprite_model, glm::vec3{world_size.x, world_size.y, 1.0f});
        }
        if (sprite->flip_x || sprite->flip_y) {
            sprite_model = glm::scale(
                    sprite_model, glm::vec3{sprite->flip_x ? -1.0f : 1.0f, sprite->flip_y ? -1.0f : 1.0f, 1.0f});
        }
        const glm::vec3 c0 = glm::vec3(sprite_model * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f));
        const glm::vec3 c1 = glm::vec3(sprite_model * glm::vec4(0.5f, -0.5f, 0.0f, 1.0f));
        const glm::vec3 c2 = glm::vec3(sprite_model * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f));
        const glm::vec3 c3 = glm::vec3(sprite_model * glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f));

        draw_edge(c0, c1);
        draw_edge(c1, c2);
        draw_edge(c2, c3);
        draw_edge(c3, c0);
    } else if (renderable != nullptr) {
        const glm::mat4 obj_model = engine::ecs::world_matrix(edit_world_, *selected);
        const glm::vec3 c0 = glm::vec3(obj_model * glm::vec4(-0.5f, -0.5f, -0.5f, 1.0f));
        const glm::vec3 c1 = glm::vec3(obj_model * glm::vec4(0.5f, -0.5f, -0.5f, 1.0f));
        const glm::vec3 c2 = glm::vec3(obj_model * glm::vec4(0.5f, 0.5f, -0.5f, 1.0f));
        const glm::vec3 c3 = glm::vec3(obj_model * glm::vec4(-0.5f, 0.5f, -0.5f, 1.0f));
        const glm::vec3 c4 = glm::vec3(obj_model * glm::vec4(-0.5f, -0.5f, 0.5f, 1.0f));
        const glm::vec3 c5 = glm::vec3(obj_model * glm::vec4(0.5f, -0.5f, 0.5f, 1.0f));
        const glm::vec3 c6 = glm::vec3(obj_model * glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        const glm::vec3 c7 = glm::vec3(obj_model * glm::vec4(-0.5f, 0.5f, 0.5f, 1.0f));

        draw_edge(c0, c1);
        draw_edge(c1, c2);
        draw_edge(c2, c3);
        draw_edge(c3, c0);

        draw_edge(c4, c5);
        draw_edge(c5, c6);
        draw_edge(c6, c7);
        draw_edge(c7, c4);

        draw_edge(c0, c4);
        draw_edge(c1, c5);
        draw_edge(c2, c6);
        draw_edge(c3, c7);
    }

    // 2. Draw 3D Gizmo
    const float s = gizmo_scale_;
    const glm::vec3 pos = gizmo_world_pos_;

    constexpr glm::vec4 kColorX{0.92f, 0.25f, 0.25f, 1.0f};
    constexpr glm::vec4 kColorY{0.30f, 0.85f, 0.35f, 1.0f};
    constexpr glm::vec4 kColorZ{0.25f, 0.50f, 0.95f, 1.0f};
    constexpr glm::vec4 kColorCenter{0.80f, 0.80f, 0.80f, 0.9f};
    constexpr glm::vec4 kColorActive{1.0f, 0.90f, 0.20f, 1.0f};

    const auto color_for = [&](GizmoAxis axis, glm::vec4 base) {
        if (gizmo_drag_axis_ == axis || (gizmo_drag_axis_ == GizmoAxis::None && gizmo_hovered_ == axis)) {
            return kColorActive;
        }
        return base;
    };

    if (gizmo_mode_ == GizmoMode::Rotate) {
        // Z Ring (blue)
        const glm::vec4 z_col = color_for(GizmoAxis::Z, kColorZ);
        const glm::quat rot_z = glm::angleAxis(glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::mat4 z_model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot_z) *
                glm::scale(glm::mat4(1.0f), glm::vec3(s));
        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_torus_,
                .material = unlit_mat,
                .model = z_model,
                .color = z_col,
        });

        if (gizmo_has_z_) {
            // Y Ring (green)
            const glm::vec4 y_col = color_for(GizmoAxis::Y, kColorY);
            const glm::mat4 y_model =
                    glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(s));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_torus_,
                    .material = unlit_mat,
                    .model = y_model,
                    .color = y_col,
            });

            // X Ring (red)
            const glm::vec4 x_col = color_for(GizmoAxis::X, kColorX);
            const glm::quat rot_x = glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
            const glm::mat4 x_model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot_x) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(s));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_torus_,
                    .material = unlit_mat,
                    .model = x_model,
                    .color = x_col,
            });
        }
        return;
    }

    // Center handle (cube)
    {
        const glm::mat4 center_model =
                glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.14f * s));
        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_cube_,
                .material = unlit_mat,
                .model = center_model,
                .color = color_for(GizmoAxis::Center, kColorCenter),
        });
    }

    const float shaft_len = 0.75f * s;
    const float shaft_rad = 0.035f * s;
    const float cone_len = 0.25f * s;
    const float cone_rad = 0.09f * s;
    const float cube_size = 0.15f * s;

    // Y Axis (points along +Y)
    {
        const glm::vec4 y_col = color_for(GizmoAxis::Y, kColorY);
        const glm::mat4 shaft_model =
                glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, shaft_len * 0.5f, 0.0f}) *
                glm::scale(glm::mat4(1.0f), glm::vec3(shaft_rad, shaft_len, shaft_rad));
        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_cylinder_,
                .material = unlit_mat,
                .model = shaft_model,
                .color = y_col,
        });
        if (gizmo_mode_ == GizmoMode::Translate) {
            const glm::mat4 cone_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, shaft_len + cone_len * 0.5f, 0.0f}) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(cone_rad, cone_len, cone_rad));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cone_,
                    .material = unlit_mat,
                    .model = cone_model,
                    .color = y_col,
            });
        } else {
            const glm::mat4 tip_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, shaft_len + cube_size * 0.5f, 0.0f}) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(cube_size));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cube_,
                    .material = unlit_mat,
                    .model = tip_model,
                    .color = y_col,
            });
        }
    }

    // X Axis (rotate by -90 deg around +Z to point along +X)
    {
        const glm::vec4 x_col = color_for(GizmoAxis::X, kColorX);
        const glm::quat rot_x = glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
        const glm::mat4 shaft_model =
                glm::translate(glm::mat4(1.0f), pos + glm::vec3{shaft_len * 0.5f, 0.0f, 0.0f}) *
                glm::mat4_cast(rot_x) * glm::scale(glm::mat4(1.0f), glm::vec3(shaft_rad, shaft_len, shaft_rad));
        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_cylinder_,
                .material = unlit_mat,
                .model = shaft_model,
                .color = x_col,
        });
        if (gizmo_mode_ == GizmoMode::Translate) {
            const glm::mat4 cone_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{shaft_len + cone_len * 0.5f, 0.0f, 0.0f}) *
                    glm::mat4_cast(rot_x) * glm::scale(glm::mat4(1.0f), glm::vec3(cone_rad, cone_len, cone_rad));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cone_,
                    .material = unlit_mat,
                    .model = cone_model,
                    .color = x_col,
            });
        } else {
            const glm::mat4 tip_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{shaft_len + cube_size * 0.5f, 0.0f, 0.0f}) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(cube_size));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cube_,
                    .material = unlit_mat,
                    .model = tip_model,
                    .color = x_col,
            });
        }
    }

    // Z Axis (if perspective: rotate by +90 deg around +X to point along +Z)
    if (gizmo_has_z_) {
        const glm::vec4 z_col = color_for(GizmoAxis::Z, kColorZ);
        const glm::quat rot_z = glm::angleAxis(glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::mat4 shaft_model =
                glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, 0.0f, shaft_len * 0.5f}) *
                glm::mat4_cast(rot_z) * glm::scale(glm::mat4(1.0f), glm::vec3(shaft_rad, shaft_len, shaft_rad));
        buffer.push(engine::render::CmdDrawMesh{
                .mesh = gizmo_cylinder_,
                .material = unlit_mat,
                .model = shaft_model,
                .color = z_col,
        });
        if (gizmo_mode_ == GizmoMode::Translate) {
            const glm::mat4 cone_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, 0.0f, shaft_len + cone_len * 0.5f}) *
                    glm::mat4_cast(rot_z) * glm::scale(glm::mat4(1.0f), glm::vec3(cone_rad, cone_len, cone_rad));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cone_,
                    .material = unlit_mat,
                    .model = cone_model,
                    .color = z_col,
            });
        } else {
            const glm::mat4 tip_model =
                    glm::translate(glm::mat4(1.0f), pos + glm::vec3{0.0f, 0.0f, shaft_len + cube_size * 0.5f}) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(cube_size));
            buffer.push(engine::render::CmdDrawMesh{
                    .mesh = gizmo_cube_,
                    .material = unlit_mat,
                    .model = tip_model,
                    .color = z_col,
            });
        }
    }
}

}
