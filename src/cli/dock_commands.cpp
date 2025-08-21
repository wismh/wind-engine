#include "cli/dock_commands.h"
#include "cli/json.h"

#include "ui/dock_runtime.h"

#include <engine/ui/dock_layout.h>
#include <engine/ui/dock_space.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::cli {
    namespace {

        using ui::DockFloatMode;
        using ui::DockLayout;
        using ui::DockNodeId;
        using ui::DockSpace;
        using ui::DockSpaceRuntime;
        using ui::DockZone;

        // A dock space of the world and its place: lowest `order` first, then entity index.
        struct SpaceRef {
            ecs::Entity entity{};
            std::uint32_t place = 0;
            DockSpace *space = nullptr;
        };

        std::vector<SpaceRef> spaces_of(ecs::World &world) {
            std::vector<SpaceRef> spaces;
            for (const ecs::Entity entity: world.view<DockSpace>()) {
                spaces.push_back(SpaceRef{entity, 0, &world.get<DockSpace>(entity)});
            }
            std::ranges::sort(spaces, [](const SpaceRef &a, const SpaceRef &b) {
                if (a.space->order != b.space->order) {
                    return a.space->order < b.space->order;
                }
                return a.entity < b.entity;
            });
            for (std::size_t i = 0; i < spaces.size(); ++i) {
                spaces[i].place = static_cast<std::uint32_t>(i);
            }
            return spaces;
        }

        const DockSpaceRuntime *runtime_of(ecs::World &world, ecs::Entity entity) {
            const ui::DockRuntime &runtime = world.ctx<ui::DockRuntime>();
            const auto it = runtime.spaces.find(entity);
            return it == runtime.spaces.end() ? nullptr : &it->second;
        }

        std::optional<WindowId> float_os_window(const DockSpaceRuntime *runtime, DockNodeId float_id) {
            return runtime == nullptr ? std::nullopt : ui::dock_float_window(*runtime, float_id);
        }

        std::optional<DockZone> parse_zone(std::string_view text) {
            if (text == "center") {
                return DockZone::Center;
            }
            if (text == "left") {
                return DockZone::Left;
            }
            if (text == "right") {
                return DockZone::Right;
            }
            if (text == "top") {
                return DockZone::Top;
            }
            if (text == "bottom") {
                return DockZone::Bottom;
            }
            return std::nullopt;
        }

        std::optional<DockFloatMode> parse_mode(std::string_view text) {
            if (text == "virtual") {
                return DockFloatMode::Virtual;
            }
            if (text == "os") {
                return DockFloatMode::OsWindow;
            }
            return std::nullopt;
        }

        std::string_view mode_name(DockFloatMode mode) {
            return mode == DockFloatMode::OsWindow ? "os" : "virtual";
        }

        void write_rect(Json &json, const render::Rect &rect) {
            json.begin_object();
            json.key("x");
            json.number(rect.x);
            json.key("y");
            json.number(rect.y);
            json.key("w");
            json.number(rect.w);
            json.key("h");
            json.number(rect.h);
            json.end_object();
        }

        void write_window(Json &json, std::optional<WindowId> window) {
            if (window) {
                json.integer(static_cast<std::uint32_t>(*window));
            } else {
                json.null();
            }
        }

        void write_identity(Json &json, const SpaceRef &ref) {
            json.key("space");
            json.integer(ref.place);
            json.key("window");
            json.integer(static_cast<std::uint32_t>(ref.space->window));
            json.key("order");
            json.integer(ref.space->order);
        }

        void write_node(Json &json, const DockLayout &layout, const ui::DockNode &node) {
            json.begin_object();
            json.key("id");
            json.integer(node.id);
            json.key("parent");
            json.integer(node.parent);
            json.key("float");
            json.integer(layout.float_of(node.id));
            if (node.kind == ui::DockNodeKind::Tabs) {
                json.key("kind");
                json.string("tabs");
                json.key("panels");
                json.begin_array();
                for (const std::string &key: node.panels) {
                    json.string(key);
                }
                json.end_array();
                json.key("active");
                json.string(node.panels[node.active]);
            } else {
                json.key("kind");
                json.string("split");
                json.key("axis");
                json.string(node.axis == ui::DockAxis::Horizontal ? "horizontal" : "vertical");
                json.key("ratio");
                json.number(node.ratio);
                json.key("first");
                json.integer(node.first);
                json.key("second");
                json.integer(node.second);
            }
            json.end_object();
        }

        void write_panel(Json &json, ecs::World &world, const SpaceRef &ref, std::string_view key) {
            const DockLayout &layout = ref.space->layout;
            const auto registered = std::ranges::find(ref.space->panels, key, &ui::DockPanel::key);
            json.begin_object();
            json.key("key");
            json.string(key);
            json.key("registered");
            json.boolean(registered != ref.space->panels.end());
            json.key("title");
            json.string(registered != ref.space->panels.end() ? std::string_view(registered->title) : "");
            json.key("closable");
            json.boolean(registered != ref.space->panels.end() && registered->closable);
            const std::optional<ui::DockPanelPlace> place = layout.find(key);
            json.key("stack");
            if (place) {
                json.integer(place->stack);
            } else {
                json.null();
            }
            json.key("index");
            if (place) {
                json.integer(static_cast<std::int64_t>(place->index));
            } else {
                json.null();
            }
            json.key("float");
            json.integer(place ? place->float_id : ui::kNoDockNode);
            json.key("visible");
            json.boolean(layout.is_visible(key));
            json.key("os_window");
            write_window(json, ui::dock_panel_os_window(world, ref.entity, key));
            json.end_object();
        }

        void write_space(Json &json, ecs::World &world, const SpaceRef &ref) {
            const DockSpace &space = *ref.space;
            const DockLayout &layout = space.layout;
            const DockSpaceRuntime *runtime = runtime_of(world, ref.entity);
            json.begin_object();
            write_identity(json, ref);
            json.key("area");
            write_rect(json, space.area);
            json.key("float_mode");
            json.string(mode_name(space.float_mode));
            json.key("revision");
            json.integer(static_cast<std::int64_t>(space.revision));
            json.key("gesture");
            json.boolean(runtime != nullptr && runtime->gesture.kind != ui::DockGestureKind::None);
            json.key("root");
            json.integer(layout.root());
            json.key("nodes");
            json.begin_array();
            for (const auto &[id, node]: layout.nodes()) {
                write_node(json, layout, node);
            }
            json.end_array();
            json.key("floats");
            json.begin_array();
            for (const ui::DockFloat &each: layout.floats()) {
                json.begin_object();
                json.key("id");
                json.integer(each.id);
                json.key("root");
                json.integer(each.root);
                json.key("rect");
                write_rect(json, each.rect);
                json.key("os_window");
                write_window(json, float_os_window(runtime, each.id));
                json.end_object();
            }
            json.end_array();
            json.key("panels");
            json.begin_array();
            for (const std::string &key: layout.panels()) {
                write_panel(json, world, ref, key);
            }
            for (const ui::DockPanel &panel: space.panels) {
                if (!layout.contains(panel.key)) {
                    write_panel(json, world, ref, panel.key);
                }
            }
            json.end_array();
            json.key("layout");
            json.string(ui::dock_layout_to_text(layout));
            json.end_object();
        }

        std::string list_json(ecs::World &world, const std::vector<SpaceRef> &spaces) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            json.begin_object();
            json.key("spaces");
            json.begin_array();
            for (const SpaceRef &ref: spaces) {
                write_space(json, world, ref);
            }
            json.end_array();
            json.end_object();
            json.end_object();
            return json.str();
        }

        std::string ambiguous_json(const std::vector<SpaceRef> &spaces) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(false);
            json.key("error");
            json.string("ambiguous");
            json.key("candidates");
            json.begin_array();
            for (const SpaceRef &ref: spaces) {
                json.begin_object();
                write_identity(json, ref);
                json.end_object();
            }
            json.end_array();
            json.end_object();
            return json.str();
        }

        std::string changed_json(const SpaceRef &ref, std::string_view action, bool changed) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            json.begin_object();
            json.key("space");
            json.integer(ref.place);
            json.key("action");
            json.string(action);
            json.key("changed");
            json.boolean(changed);
            json.key("revision");
            json.integer(static_cast<std::int64_t>(ref.space->revision));
            json.key("float_mode");
            json.string(mode_name(ref.space->float_mode));
            json.key("layout");
            json.string(ui::dock_layout_to_text(ref.space->layout));
            json.end_object();
            json.end_object();
            return json.str();
        }

        // Where a panel floated without a frame goes: DockMetrics::float_size, centered in the dock area.
        render::Rect default_float_rect(const DockSpace &space) {
            const glm::vec2 size = space.metrics.float_size;
            return render::Rect{space.area.x + (space.area.w - size.x) * 0.5f,
                                space.area.y + (space.area.h - size.y) * 0.5f, size.x, size.y};
        }

        // Applies one layout operation the way the dock system applies a user's: the change bumps the revision.
        // A tab activated in a virtual float raises it, as a press on that tab does.
        std::string change_layout(ecs::World &world, const SpaceRef &ref, const CliRequest &request) {
            DockSpace &space = *ref.space;
            DockLayout &layout = space.layout;
            const DockLayout before = layout;
            const std::optional<ui::DockPanelPlace> place = layout.find(request.panel);
            if (request.action == "activate") {
                layout.activate(request.panel);
                if (place->float_id != ui::kNoDockNode &&
                    !float_os_window(runtime_of(world, ref.entity), place->float_id).has_value() &&
                    layout.floats().back().id != place->float_id) {
                    layout.raise_float(place->float_id);
                }
            } else if (request.action == "move") {
                const DockNodeId node = request.node.value_or(ui::kNoDockNode);
                if (node != ui::kNoDockNode && layout.node(node) == nullptr) {
                    return error_json(std::format("no node {}", node));
                }
                const std::optional<DockZone> zone = parse_zone(request.zone.empty() ? "center" : request.zone);
                if (!zone) {
                    return error_json(std::format("unknown zone {}", request.zone));
                }
                layout.move(request.panel, ui::DockTarget{node, *zone});
            } else {
                render::Rect rect = default_float_rect(space);
                if (request.has_x || request.has_y || request.has_w || request.has_h) {
                    if (!(request.has_x && request.has_y && request.has_w && request.has_h) || request.w <= 0.0 ||
                        request.h <= 0.0) {
                        return error_json("float needs x, y, and a positive w and h");
                    }
                    rect = render::Rect{static_cast<float>(request.x), static_cast<float>(request.y),
                                        static_cast<float>(request.w), static_cast<float>(request.h)};
                }
                layout.float_panel(request.panel, rect);
            }
            const bool changed = layout != before;
            if (changed) {
                ++space.revision;
            }
            return changed_json(ref, request.action, changed);
        }

    } // namespace

    std::string dock_json(ecs::World &world, const CliRequest &request) {
        std::vector<SpaceRef> spaces = spaces_of(world);
        if (request.space) {
            if (*request.space >= spaces.size()) {
                return error_json(std::format("no dock space {} on window {}", *request.space, request.window));
            }
            spaces = {spaces[*request.space]};
        }
        if (spaces.empty()) {
            return error_json(std::format("no dock space on window {}", request.window));
        }
        if (request.action.empty()) {
            return list_json(world, spaces);
        }
        const bool panel_action =
                request.action == "activate" || request.action == "move" || request.action == "float";
        if (!panel_action && request.action != "mode") {
            return error_json(std::format("unknown dock action {}", request.action));
        }
        if (panel_action) {
            if (request.panel.empty()) {
                return error_json(std::format("{} needs a panel", request.action));
            }
            std::erase_if(spaces, [&](const SpaceRef &ref) { return !ref.space->layout.contains(request.panel); });
            if (spaces.empty()) {
                return error_json(std::format("no panel {}", request.panel));
            }
        }
        if (spaces.size() > 1) {
            return ambiguous_json(spaces);
        }
        const SpaceRef &ref = spaces.front();
        if (const DockSpaceRuntime *runtime = runtime_of(world, ref.entity);
            runtime != nullptr && runtime->gesture.kind != ui::DockGestureKind::None) {
            return error_json("a dock gesture is in progress");
        }
        if (panel_action) {
            return change_layout(world, ref, request);
        }
        const std::optional<DockFloatMode> mode = parse_mode(request.mode);
        if (!mode) {
            return error_json(std::format("unknown float mode {}", request.mode));
        }
        const bool changed = ref.space->float_mode != *mode;
        ref.space->float_mode = *mode;
        return changed_json(ref, request.action, changed);
    }

} // namespace engine::cli
