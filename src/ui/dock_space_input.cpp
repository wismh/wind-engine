#include "ui/dock_runtime.h"

#include <engine/core/window_control.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include "ui/popup.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace engine::ui {

    namespace {

        [[nodiscard]] bool inside(const render::Rect &r, glm::vec2 p) { return rect_contains(r, p.x, p.y); }

        // A docked tab strip or splitter, or anywhere on a virtual float: what the dock space answers for in its own
        // window.
        [[nodiscard]] bool on_chrome(const DockGeometry &geometry, glm::vec2 p) {
            if (dock_float_at(geometry, p) != kNoDockNode) {
                return true;
            }
            if (!inside(geometry.area, p)) {
                return false;
            }
            const bool strip = std::ranges::any_of(geometry.stacks, [&](const DockStackRect &s) {
                return s.float_id == kNoDockNode && inside(s.strip, p);
            });
            return strip || std::ranges::any_of(geometry.splitters, [&](const DockSplitterRect &s) {
                       return s.float_id == kNoDockNode && inside(s.grab, p);
                   });
        }

        // A tab strip or splitter of the float in an OS window. Its panels' content is theirs, as when docked.
        [[nodiscard]] bool on_window_chrome(const DockGeometry &geometry, glm::vec2 p) {
            return std::ranges::any_of(geometry.stacks, [&](const DockStackRect &s) { return inside(s.strip, p); }) ||
                   std::ranges::any_of(geometry.splitters,
                                       [&](const DockSplitterRect &s) { return inside(s.grab, p); });
        }

        [[nodiscard]] bool owns_canvas(const DockSpace &space, const DockSpaceRuntime &runtime, ecs::Entity entity) {
            if (entity == runtime.docked.canvas || entity == runtime.preview.canvas) {
                return true;
            }
            if (std::ranges::any_of(runtime.floats, [&](const auto &entry) { return entry.second.canvas == entity; })) {
                return true;
            }
            return std::ranges::any_of(space.panels, [&](const DockPanel &panel) { return panel.canvas == entity; });
        }

        [[nodiscard]] glm::vec2 clamp_to(const render::Rect &area, glm::vec2 p) {
            if (area.w <= 0.0f || area.h <= 0.0f) {
                return p;
            }
            return {std::clamp(p.x, area.x, area.x + area.w), std::clamp(p.y, area.y, area.y + area.h)};
        }

        [[nodiscard]] render::Rect moved(render::Rect rect, glm::vec2 delta) {
            rect.x += delta.x;
            rect.y += delta.y;
            return rect;
        }

        [[nodiscard]] glm::vec2 to_vec(glm::ivec2 v) { return {static_cast<float>(v.x), static_cast<float>(v.y)}; }

        [[nodiscard]] bool alone_in_float(const DockLayout &layout, std::string_view key, DockNodeId &float_id) {
            const std::optional<DockPanelPlace> place = layout.find(key);
            if (!place || place->float_id == kNoDockNode) {
                return false;
            }
            const DockFloat *f = layout.find_float(place->float_id);
            if (f->root != place->stack || layout.node(place->stack)->panels.size() != 1) {
                return false;
            }
            float_id = place->float_id;
            return true;
        }

        // Input of one dock space for events of one of its windows: the space's own (docked tree, virtual floats) or
        // the OS window of one of its floats.
        class SpaceInput {
        public:
            SpaceInput(ecs::World &world, ecs::Entity entity, DockSpaceRuntime &runtime, const EngineSystemDeps &deps,
                       WindowId window) :
                world_(world), entity_(entity), space_(world.get<DockSpace>(entity)), runtime_(runtime), deps_(deps),
                gesture_(runtime.gesture), window_(window), float_window_(dock_float_in_window(runtime, window)) {
                refresh();
            }

            // Returns true when the space took the press, so spaces below do not see it.
            bool down(glm::vec2 p, MouseButton button) {
                if (gesture_.kind != DockGestureKind::None) {
                    // Another button while dragging: the drag is off.
                    cancel();
                    consume();
                    return true;
                }
                if (!chrome_at(p) || blocked(p)) {
                    return false;
                }
                consume();
                if (const DockNodeId float_id = dock_float_at(geometry_, p);
                    float_window_ == kNoDockNode && float_id != kNoDockNode &&
                    space_.layout.floats().back().id != float_id) {
                    space_.layout.raise_float(float_id);
                    changed();
                }
                if (button != MouseButton::Left) {
                    return true;
                }
                const std::optional<DockChromeHit> hit = dock_chrome_at(geometry_, metrics_, p);
                if (!hit) {
                    return true;
                }
                gesture_ = DockGesture{};
                gesture_.window = window_;
                gesture_.press = p;
                gesture_.node = hit->node;
                switch (hit->kind) {
                    case DockChromeKind::Tab:
                        press_tab(*hit, p);
                        break;
                    case DockChromeKind::Splitter:
                        if (const DockSplitterRect *splitter = geometry_.splitter(hit->node)) {
                            gesture_.kind = DockGestureKind::Splitter;
                            gesture_.splitter = *splitter;
                            gesture_.start_ratio = space_.layout.node(hit->node)->ratio;
                        }
                        break;
                    case DockChromeKind::FloatTitle:
                        start_float_move(hit->node);
                        break;
                    case DockChromeKind::FloatEdge:
                        gesture_.kind = DockGestureKind::FloatResize;
                        gesture_.edges = hit->edges;
                        gesture_.start_rect = geometry_.floating(hit->node)->frame;
                        gesture_.stored_rect = space_.layout.find_float(hit->node)->rect;
                        break;
                }
                return true;
            }

            void move(glm::vec2 p) {
                if (gesture_.kind == DockGestureKind::None) {
                    if (chrome_at(p) && !blocked(p)) {
                        consume();
                    }
                    return;
                }
                consume();
                if (window_ == gesture_.window) {
                    drag(p);
                }
            }

            void up(MouseButton button) {
                if (gesture_.kind == DockGestureKind::None || button != MouseButton::Left) {
                    return;
                }
                commit();
            }

            void wheel(glm::vec2 p) {
                if (gesture_.kind != DockGestureKind::None || (chrome_at(p) && !blocked(p))) {
                    consume();
                }
            }

            // Puts back what the gesture changed so far.
            void cancel() {
                switch (gesture_.kind) {
                    case DockGestureKind::Splitter:
                        space_.layout.set_ratio(gesture_.node, gesture_.start_ratio);
                        break;
                    case DockGestureKind::FloatMove:
                    case DockGestureKind::FloatResize:
                        space_.layout.set_float_rect(gesture_.node, gesture_.stored_rect);
                        break;
                    default:
                        break;
                }
                gesture_ = DockGesture{};
            }

        private:
            // The geometry and metrics of this input's window.
            void refresh() {
                if (float_window_ != kNoDockNode) {
                    metrics_ = dock_os_float_metrics(dock_space_metrics(space_, runtime_));
                    geometry_ = dock_float_window_geometry(space_, runtime_, float_window_);
                } else {
                    metrics_ = dock_space_metrics(space_, runtime_);
                    geometry_ = dock_space_geometry(space_, runtime_);
                }
            }

            [[nodiscard]] bool chrome_at(glm::vec2 p) const {
                return float_window_ != kNoDockNode ? on_window_chrome(geometry_, p) : on_chrome(geometry_, p);
            }

            // Another canvas above the chrome under `p` takes the pointer there: an open popup anywhere in the
            // window, or a canvas the space does not own with a higher order whose rect holds the point.
            [[nodiscard]] bool blocked(glm::vec2 p) {
                if (popup_canvas_at(world_, window_, p).has_value()) {
                    return true;
                }
                int layer = dock_docked_chrome_order(space_);
                if (float_window_ != kNoDockNode) {
                    layer = dock_float_panel_order(space_, float_window_) - 1;
                } else if (const DockNodeId float_id = dock_float_at(geometry_, p); float_id != kNoDockNode) {
                    layer = dock_float_panel_order(space_, float_id) - 1;
                }
                auto view = world_.view<UiCanvas>();
                for (const ecs::Entity entity: view) {
                    const UiCanvas &canvas = view.get<UiCanvas>(entity);
                    if (canvas.window == window_ && canvas.order > layer && inside(canvas.rect, p) &&
                        !owns_canvas(space_, runtime_, entity)) {
                        return true;
                    }
                }
                return false;
            }

            void consume() { presentation_of(world_).mouse.consumed_windows.insert(window_); }

            void changed() {
                ++space_.revision;
                refresh();
            }

            [[nodiscard]] bool shift() { return world_.ctx<UiModifierState>().modifiers[window_].shift; }

            // Floats become OS windows: OsWindow mode with window control, and no window refused to open.
            [[nodiscard]] bool os_floats() const {
                return space_.float_mode == DockFloatMode::OsWindow && deps_.windows != nullptr &&
                       deps_.worlds != nullptr && !runtime_.windows_failed;
            }

            void press_tab(const DockChromeHit &hit, glm::vec2 p) {
                const DockStackRect *stack = geometry_.stack(hit.node);
                // A copy: activating recomputes the geometry.
                const DockTabRect tab = *std::ranges::find(stack->tabs, hit.key, &DockTabRect::key);
                const DockPanel *panel = dock_panel(space_, hit.key);
                if (panel != nullptr && panel->closable &&
                    inside(dock_close_button_rect(tab.rect, space_.close_button_size), p)) {
                    ecs::EventWriter<DockPanelCloseRequested>{world_}.send(DockPanelCloseRequested{entity_, hit.key});
                    return;
                }
                if (!tab.active) {
                    space_.layout.activate(hit.key);
                    changed();
                }
                gesture_.kind = DockGestureKind::TabPress;
                gesture_.key = hit.key;
                // Where the pointer sits in the float the panel would make: its client area has no frame or title bar
                // when floats are OS windows.
                const DockMetrics &m = space_.metrics;
                gesture_.grab = os_floats() ? glm::vec2{p.x - tab.rect.x, p.y - tab.rect.y}
                                            : glm::vec2{p.x - tab.rect.x + m.frame_border,
                                                        p.y - tab.rect.y + m.frame_border + m.title_bar_height};
            }

            void start_float_move(DockNodeId float_id) {
                gesture_.kind = DockGestureKind::FloatMove;
                gesture_.node = float_id;
                gesture_.start_rect = geometry_.floating(float_id)->frame;
                gesture_.stored_rect = space_.layout.find_float(float_id)->rect;
            }

            // The window under screen point `screen` among the space's: its float windows top first, then its own.
            // Nullopt over neither.
            [[nodiscard]] std::optional<WindowId> window_at(glm::ivec2 screen) const {
                const IWindowControl &control = *deps_.windows;
                const auto holds = [&](WindowId window) {
                    const std::optional<glm::ivec2> position = control.position(window);
                    const std::optional<glm::ivec2> size = control.size(window);
                    return position && size && screen.x >= position->x && screen.y >= position->y &&
                           screen.x < position->x + size->x && screen.y < position->y + size->y;
                };
                const std::span<const DockFloat> floats = space_.layout.floats();
                for (auto it = floats.rbegin(); it != floats.rend(); ++it) {
                    if (const std::optional<WindowId> window = dock_float_window(runtime_, it->id);
                        window && holds(*window)) {
                        return window;
                    }
                }
                if (holds(space_.window)) {
                    return space_.window;
                }
                return std::nullopt;
            }

            // A new float with the pointer at `grab` inside it, the pointer at `local` in the space's window.
            [[nodiscard]] DockDrop float_drop(glm::vec2 local) const {
                const DockMetrics &m = space_.metrics;
                return DockDrop{DockDropKind::Float,
                                {},
                                {local.x - gesture_.grab.x, local.y - gesture_.grab.y, m.float_size.x,
                                 m.float_size.y}};
            }

            // A tab dragged while floats are OS windows: the drop is judged in whichever of the space's windows is
            // under the pointer on screen, and a drop over none of them (or with Shift) is a new OS window there.
            void drag_tab_across_windows(glm::vec2 p) {
                gesture_.drop.reset();
                gesture_.preview_window.reset();
                const glm::ivec2 origin = dock_space_screen_origin(space_, deps_);
                const std::optional<glm::ivec2> here = deps_.windows->position(window_);
                // Screen pixels of the pointer; the window's own pixels when the OS does not say where it is.
                const glm::ivec2 screen =
                        here ? *here + glm::ivec2{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))}
                             : glm::ivec2{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
                const glm::vec2 in_space = here ? to_vec(screen - origin) : p;
                if (shift()) {
                    gesture_.drop = float_drop(in_space);
                    return;
                }
                const std::optional<WindowId> target = here ? window_at(screen) : std::optional<WindowId>{window_};
                if (!target) {
                    gesture_.drop = float_drop(in_space);
                    return;
                }
                const DockNodeId target_float = dock_float_in_window(runtime_, *target);
                if (target_float == kNoDockNode) {
                    const DockGeometry geometry = dock_space_geometry(space_, runtime_);
                    const DockMetrics metrics = dock_space_metrics(space_, runtime_);
                    std::optional<DockDrop> drop = dock_drop_for_panel(space_.layout, geometry, metrics, in_space,
                                                                       gesture_.key, gesture_.grab);
                    if (drop && drop->kind == DockDropKind::Float) {
                        gesture_.drop = float_drop(in_space);
                    } else if (drop) {
                        gesture_.drop = drop;
                        gesture_.preview_window = space_.window;
                        gesture_.preview_rect = drop->preview;
                    }
                    return;
                }
                DockNodeId own = kNoDockNode;
                if (alone_in_float(space_.layout, gesture_.key, own) && own == target_float) {
                    // Over its own window, which holds nothing else: no drop.
                    return;
                }
                const glm::vec2 local = here ? to_vec(screen - *deps_.windows->position(*target)) : p;
                const DockGeometry geometry = dock_float_window_geometry(space_, runtime_, target_float);
                const DockMetrics metrics = dock_os_float_metrics(dock_space_metrics(space_, runtime_));
                std::optional<DockDrop> drop =
                        dock_drop_for_panel(space_.layout, geometry, metrics, local, gesture_.key, gesture_.grab);
                if (drop && drop->kind == DockDropKind::Dock) {
                    gesture_.drop = drop;
                    gesture_.preview_window = *target;
                    gesture_.preview_rect = drop->preview;
                }
            }

            void drag(glm::vec2 p) {
                DockLayout &layout = space_.layout;
                const DockMetrics &m = metrics_;
                if (gesture_.kind == DockGestureKind::TabPress) {
                    const glm::vec2 travel = p - gesture_.press;
                    if (std::hypot(travel.x, travel.y) < space_.drag_threshold) {
                        return;
                    }
                    DockNodeId float_id = kNoDockNode;
                    if (alone_in_float(layout, gesture_.key, float_id) && !runtime_.windows.contains(float_id)) {
                        // The panel is the whole virtual float: dragging its tab drags the float.
                        start_float_move(float_id);
                    } else if (layout.contains(gesture_.key)) {
                        gesture_.kind = DockGestureKind::TabDrag;
                    } else {
                        gesture_ = DockGesture{};
                        return;
                    }
                }
                switch (gesture_.kind) {
                    case DockGestureKind::TabDrag:
                        if (os_floats()) {
                            drag_tab_across_windows(p);
                            break;
                        }
                        if (shift()) {
                            gesture_.drop = DockDrop{
                                    DockDropKind::Float,
                                    {},
                                    {p.x - gesture_.grab.x, p.y - gesture_.grab.y, m.float_size.x, m.float_size.y}};
                        } else {
                            gesture_.drop = dock_drop_for_panel(layout, geometry_, m, p, gesture_.key, gesture_.grab);
                        }
                        if (gesture_.drop && gesture_.drop->kind == DockDropKind::Float) {
                            gesture_.drop->preview =
                                    dock_float_home(space_, runtime_, kNoDockNode, gesture_.drop->preview).rect;
                        }
                        show_preview_here();
                        break;
                    case DockGestureKind::Splitter:
                        layout.set_ratio(gesture_.node, dock_split_ratio_at(gesture_.splitter, p, m));
                        refresh();
                        break;
                    case DockGestureKind::FloatMove: {
                        const render::Rect to = moved(gesture_.start_rect, p - gesture_.press);
                        layout.set_float_rect(gesture_.node, dock_float_home(space_, runtime_, gesture_.node, to).rect);
                        refresh();
                        gesture_.drop =
                                shift() ? std::nullopt : dock_drop_for_float(layout, geometry_, m, p, gesture_.node);
                        show_preview_here();
                        break;
                    }
                    case DockGestureKind::FloatResize: {
                        const glm::vec2 delta = clamp_to(space_.area, p) - gesture_.press;
                        layout.set_float_rect(gesture_.node,
                                              dock_float_resized(gesture_.start_rect, gesture_.edges, delta, m));
                        refresh();
                        break;
                    }
                    default:
                        break;
                }
            }

            // The drop's preview in this input's window.
            void show_preview_here() {
                gesture_.preview_window.reset();
                if (gesture_.drop) {
                    gesture_.preview_window = window_;
                    gesture_.preview_rect = gesture_.drop->preview;
                }
            }

            void commit() {
                DockLayout &layout = space_.layout;
                bool done = false;
                switch (gesture_.kind) {
                    case DockGestureKind::TabDrag:
                        if (gesture_.drop) {
                            done = gesture_.drop->kind == DockDropKind::Dock
                                           ? layout.move(gesture_.key, gesture_.drop->target)
                                           : layout.float_panel(gesture_.key, gesture_.drop->preview);
                        }
                        break;
                    case DockGestureKind::Splitter:
                        done = layout.node(gesture_.node) != nullptr &&
                               layout.node(gesture_.node)->ratio != gesture_.start_ratio;
                        break;
                    case DockGestureKind::FloatMove:
                        if (gesture_.drop) {
                            done = layout.dock_float(gesture_.node, gesture_.drop->target);
                        }
                        if (!done) {
                            const DockFloat *f = layout.find_float(gesture_.node);
                            done = f != nullptr && f->rect != gesture_.stored_rect;
                        }
                        break;
                    case DockGestureKind::FloatResize: {
                        const DockFloat *f = layout.find_float(gesture_.node);
                        done = f != nullptr && f->rect != gesture_.stored_rect;
                        break;
                    }
                    default:
                        break;
                }
                gesture_ = DockGesture{};
                if (done) {
                    changed();
                }
            }

            ecs::World &world_;
            ecs::Entity entity_;
            DockSpace &space_;
            DockSpaceRuntime &runtime_;
            const EngineSystemDeps &deps_;
            DockGesture &gesture_;
            WindowId window_;
            // The float whose OS window this is, or kNoDockNode for the space's own window.
            DockNodeId float_window_ = kNoDockNode;
            DockMetrics metrics_;
            DockGeometry geometry_;
        };

        [[nodiscard]] std::vector<ecs::Entity> spaces_top_first(ecs::World &world) {
            std::vector<ecs::Entity> entities;
            for (const ecs::Entity entity: world.view<DockSpace>()) {
                entities.push_back(entity);
            }
            std::ranges::stable_sort(entities, [&](ecs::Entity a, ecs::Entity b) {
                return world.get<DockSpace>(a).order > world.get<DockSpace>(b).order;
            });
            return entities;
        }

    } // namespace

    void run_dock_input(ecs::World &world, const EngineSystemDeps &deps) {
        DockRuntime &runtime = world.ctx<DockRuntime>();
        std::vector<MouseEvent> mouse;
        for (const MouseEvent &event: ecs::EventReader<MouseEvent>{world, runtime.mouse}) {
            mouse.push_back(event);
        }
        std::vector<KeyEvent> keys;
        for (const KeyEvent &event: ecs::EventReader<KeyEvent>{world, runtime.keys}) {
            keys.push_back(event);
        }
        std::vector<WindowId> closes;
        for (const WindowCloseRequestedEvent &event:
             ecs::EventReader<WindowCloseRequestedEvent>{world, runtime.closes}) {
            closes.push_back(event.window);
        }
        const std::vector<ecs::Entity> entities = spaces_top_first(world);
        const auto owns = [&](ecs::Entity entity, WindowId window) {
            return dock_space_owns_window(world.get<DockSpace>(entity), runtime.spaces[entity], window);
        };
        // The close button of a float's OS window docks the float back; the window closes in the layout pass.
        for (const WindowId window: closes) {
            for (const ecs::Entity entity: entities) {
                DockSpaceRuntime &space_runtime = runtime.spaces[entity];
                const DockNodeId float_id = dock_float_in_window(space_runtime, window);
                if (float_id == kNoDockNode) {
                    continue;
                }
                if (space_runtime.gesture.kind != DockGestureKind::None) {
                    SpaceInput(world, entity, space_runtime, deps, window).cancel();
                }
                DockSpace &space = world.get<DockSpace>(entity);
                if (dock_float_window_closed(space, float_id)) {
                    ++space.revision;
                }
            }
        }
        for (const KeyEvent &key: keys) {
            if (key.key != KeyCode::Escape || !key.down) {
                continue;
            }
            for (const ecs::Entity entity: entities) {
                if (owns(entity, key.window)) {
                    SpaceInput(world, entity, runtime.spaces[entity], deps, key.window).cancel();
                }
            }
        }
        for (const MouseEvent &event: mouse) {
            bool taken = false;
            for (const ecs::Entity entity: entities) {
                if (!owns(entity, event.window)) {
                    continue;
                }
                SpaceInput input(world, entity, runtime.spaces[entity], deps, event.window);
                switch (event.kind) {
                    case MouseEvent::Kind::Down:
                        if (!taken) {
                            taken = input.down(event.position, event.button);
                        }
                        break;
                    case MouseEvent::Kind::Move:
                        input.move(event.position);
                        break;
                    case MouseEvent::Kind::Up:
                        input.up(event.button);
                        break;
                    case MouseEvent::Kind::Wheel:
                        input.wheel(event.position);
                        break;
                }
            }
        }
        // A button lost without an Up (focus went elsewhere): the drag is off.
        for (const ecs::Entity entity: entities) {
            DockSpaceRuntime &space_runtime = runtime.spaces[entity];
            const DockGesture &gesture = space_runtime.gesture;
            if (gesture.kind != DockGestureKind::None && !pointer_for(world, gesture.window).down) {
                SpaceInput(world, entity, space_runtime, deps, gesture.window).cancel();
            }
        }
    }

} // namespace engine::ui
