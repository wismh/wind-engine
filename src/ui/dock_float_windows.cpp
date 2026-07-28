#include "ui/dock_runtime.h"

#include <engine/core/window_control.h>
#include <engine/core/worlds.h>
#include <engine/log.h>

#include <algorithm>
#include <cmath>
#include <vector>

// docs/tech/features/Docking.md#os-window-floats

namespace engine::ui {

    namespace {

        [[nodiscard]] bool has_area(const render::Rect &rect) { return rect.w > 0.0f && rect.h > 0.0f; }

        [[nodiscard]] bool can_open(const EngineSystemDeps &deps) {
            return deps.windows != nullptr && deps.worlds != nullptr;
        }

        [[nodiscard]] glm::ivec2 rounded(float x, float y) {
            return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
        }

        // Window size of a float rect: whole pixels, at least one.
        [[nodiscard]] glm::ivec2 window_size(const render::Rect &rect) {
            const glm::ivec2 size = rounded(rect.w, rect.h);
            return {std::max(size.x, 1), std::max(size.y, 1)};
        }

        // The title of a float's window: the active panel of its first stack.
        [[nodiscard]] std::string float_title(const DockSpace &space, const DockFloat &f) {
            const std::vector<DockNodeId> stacks = space.layout.stacks_under(f.root);
            if (stacks.empty()) {
                return {};
            }
            const DockNode *stack = space.layout.node(stacks.front());
            if (stack == nullptr || stack->panels.empty()) {
                return {};
            }
            return dock_panel_title(space, stack->panels[std::min(stack->active, stack->panels.size() - 1)]);
        }

        [[nodiscard]] bool intersects(const render::Rect &a, const render::Rect &b) {
            return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
        }

        // `rect` (screen pixels) when the top of it is on a display, else moved onto the display of `owner`. Displays
        // that cannot be read leave it as it is.
        [[nodiscard]] render::Rect on_a_display(const IWindowControl &control, WindowId owner, render::Rect rect) {
            std::vector<render::Rect> displays;
            const auto add = [&](const render::Rect &bounds) {
                if (has_area(bounds) && std::ranges::find(displays, bounds) == displays.end()) {
                    displays.push_back(bounds);
                }
            };
            add(control.usable_display_bounds_for_window(owner));
            // An index past the last display answers the primary one, so a few indices cover a usual desk.
            constexpr int kDisplaysTried = 8;
            for (int i = 0; i < kDisplaysTried; ++i) {
                add(control.usable_display_bounds(i));
            }
            if (displays.empty()) {
                return rect;
            }
            // The OS title bar sits above the client area; the top strip of the client area is what must show.
            constexpr float kGrip = 24.0f;
            const render::Rect top{rect.x, rect.y, rect.w, std::min(rect.h, kGrip)};
            if (std::ranges::any_of(displays, [&](const render::Rect &d) { return intersects(top, d); })) {
                return rect;
            }
            return dock_float_clamped(rect, displays.front());
        }

        void close_window(DockFloatWindow &entry, const EngineSystemDeps &deps) {
            if (deps.worlds != nullptr) {
                deps.worlds->unbind_window(entry.window);
            }
            if (deps.windows != nullptr) {
                deps.windows->close_window(entry.window);
            }
        }

        // Opens the window of float `f` at its stored rect (moved onto a display when it is off every one) and binds
        // it to `world`. False when the window does not open.
        bool open_window(ecs::World &world, DockSpace &space, DockSpaceRuntime &runtime, const DockFloat &f,
                         glm::ivec2 origin, const EngineSystemDeps &deps) {
            IWindowControl &control = *deps.windows;
            const glm::ivec2 size = window_size(f.rect);
            const glm::ivec2 position = rounded(static_cast<float>(origin.x) + f.rect.x,
                                                static_cast<float>(origin.y) + f.rect.y);
            const render::Rect wanted{static_cast<float>(position.x), static_cast<float>(position.y),
                                      static_cast<float>(size.x), static_cast<float>(size.y)};
            const render::Rect placed = on_a_display(control, space.window, wanted);
            WindowDesc desc;
            desc.title = float_title(space, f);
            desc.size = size;
            desc.position = rounded(placed.x, placed.y);
            // A tool window of the space's window: above it, minimized with it, no taskbar entry of its own.
            desc.style.utility = true;
            desc.owner = space.window;
            const std::optional<WindowId> window = control.open_window(desc);
            if (!window) {
                return false;
            }
            deps.worlds->bind_window(*window, world);
            DockFloatWindow entry;
            entry.window = *window;
            entry.position = control.position(*window).value_or(*desc.position);
            entry.size = control.size(*window).value_or(desc.size);
            entry.title = desc.title;
            entry.rect = f.rect;
            if (entry.position != position || entry.size != size) {
                // Moved onto a display, or the OS placed it elsewhere: the layout follows the window.
                entry.rect = render::Rect{static_cast<float>(entry.position.x - origin.x),
                                          static_cast<float>(entry.position.y - origin.y),
                                          static_cast<float>(entry.size.x), static_cast<float>(entry.size.y)};
                space.layout.set_float_rect(f.id, entry.rect);
                ++space.revision;
            }
            runtime.windows[f.id] = std::move(entry);
            return true;
        }

    } // namespace

    glm::ivec2 dock_space_screen_origin(const DockSpace &space, const EngineSystemDeps &deps) {
        if (deps.windows != nullptr) {
            if (const std::optional<glm::ivec2> position = deps.windows->position(space.window)) {
                return *position;
            }
        }
        return {0, 0};
    }

    void close_dock_float_windows(DockSpaceRuntime &runtime, const EngineSystemDeps &deps) {
        for (auto &[id, entry]: runtime.windows) {
            close_window(entry, deps);
        }
        runtime.windows.clear();
    }

    void close_world_dock_float_windows(ecs::World &world, const EngineSystemDeps &deps) {
        for (auto &[entity, runtime]: world.ctx<DockRuntime>().spaces) {
            close_dock_float_windows(runtime, deps);
        }
    }

    void sync_dock_float_windows(ecs::World &world, ecs::Entity entity, DockSpaceRuntime &runtime,
                                 const EngineSystemDeps &deps) {
        DockSpace &space = world.get<DockSpace>(entity);
        if (space.float_mode != runtime.windows_mode) {
            runtime.windows_mode = space.float_mode;
            runtime.windows_failed = false;
        }
        if (space.float_mode != DockFloatMode::OsWindow || !can_open(deps) || runtime.windows_failed) {
            close_dock_float_windows(runtime, deps);
            return;
        }
        IWindowControl &control = *deps.windows;
        for (auto it = runtime.windows.begin(); it != runtime.windows.end();) {
            if (space.layout.find_float(it->first) == nullptr) {
                // The float went (docked, emptied): its window closes.
                close_window(it->second, deps);
                it = runtime.windows.erase(it);
            } else if (!control.position(it->second.window)) {
                // Something else closed the window: the float gets a new one below.
                deps.worlds->unbind_window(it->second.window);
                it = runtime.windows.erase(it);
            } else {
                ++it;
            }
        }
        const glm::ivec2 origin = dock_space_screen_origin(space, deps);
        std::vector<DockNodeId> ids;
        for (const DockFloat &f: space.layout.floats()) {
            ids.push_back(f.id);
        }
        for (const DockNodeId id: ids) {
            const DockFloat f = *space.layout.find_float(id);
            const auto it = runtime.windows.find(id);
            if (it == runtime.windows.end()) {
                if (!open_window(world, space, runtime, f, origin, deps)) {
                    log::warn("Dock: cannot open a window for a float; floats stay virtual");
                    runtime.windows_failed = true;
                    close_dock_float_windows(runtime, deps);
                    return;
                }
                continue;
            }
            DockFloatWindow &entry = it->second;
            const glm::ivec2 position = control.position(entry.window).value_or(entry.position);
            const glm::ivec2 size = control.size(entry.window).value_or(entry.size);
            if (position != entry.position || size != entry.size) {
                // Moved or resized by the user: the layout follows, in the space window's client pixels.
                entry.position = position;
                entry.size = size;
                entry.rect = render::Rect{static_cast<float>(position.x - origin.x),
                                          static_cast<float>(position.y - origin.y), static_cast<float>(size.x),
                                          static_cast<float>(size.y)};
                if (entry.rect != f.rect) {
                    space.layout.set_float_rect(id, entry.rect);
                    ++space.revision;
                }
            } else if (f.rect != entry.rect) {
                // The layout moved or resized it (a drop, the host): the window follows.
                const glm::ivec2 to = rounded(static_cast<float>(origin.x) + f.rect.x,
                                              static_cast<float>(origin.y) + f.rect.y);
                const glm::ivec2 to_size = window_size(f.rect);
                if (to != entry.position) {
                    control.set_position(to, entry.window);
                }
                if (to_size != entry.size) {
                    control.resize(to_size, entry.window);
                }
                entry.position = control.position(entry.window).value_or(to);
                entry.size = control.size(entry.window).value_or(to_size);
                entry.rect = f.rect;
            }
            const std::string title = float_title(space, f);
            if (title != entry.title) {
                control.set_title(title, entry.window);
                entry.title = title;
            }
        }
    }

    bool dock_float_window_closed(DockSpace &space, DockNodeId float_id) {
        DockLayout &layout = space.layout;
        const DockFloat *f = layout.find_float(float_id);
        if (f == nullptr) {
            return false;
        }
        if (layout.root() == kNoDockNode) {
            return layout.dock_float(float_id, DockTarget{kNoDockNode, DockZone::Center});
        }
        if (layout.node(f->root)->kind == DockNodeKind::Tabs) {
            const std::vector<DockNodeId> stacks = layout.stacks_under(layout.root());
            if (!stacks.empty() && layout.dock_float(float_id, DockTarget{stacks.front(), DockZone::Center})) {
                return true;
            }
        }
        return layout.dock_float(float_id, DockTarget{kNoDockNode, DockZone::Right});
    }

} // namespace engine::ui
