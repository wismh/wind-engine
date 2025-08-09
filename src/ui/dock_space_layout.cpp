#include "ui/dock_runtime.h"

#include <engine/ui/canvas.h>

#include "ui/popup.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ui {

    namespace {

        [[nodiscard]] bool has_area(const render::Rect &rect) { return rect.w > 0.0f && rect.h > 0.0f; }

        // Drops from `geometry` every float, with its stacks, splitters and panels, that `keep` refuses. The docked
        // tree stays.
        template<typename Keep>
        void keep_floats(DockGeometry &geometry, Keep keep) {
            const auto gone = [&](DockNodeId float_id) { return float_id != kNoDockNode && !keep(float_id); };
            std::erase_if(geometry.panels, [&](const DockPanelRect &p) { return gone(p.float_id); });
            std::erase_if(geometry.stacks, [&](const DockStackRect &s) { return gone(s.float_id); });
            std::erase_if(geometry.splitters, [&](const DockSplitterRect &s) { return gone(s.float_id); });
            std::erase_if(geometry.floats, [&](const DockFloatRect &f) { return gone(f.id); });
        }

        DockChromeCanvas spawn_chrome(ecs::World &world, const DockSpace &space) {
            DockChromeCanvas chrome;
            chrome.vm = std::make_shared<DockChromeViewModel>();
            UiCanvas canvas;
            canvas.fit = UiFit::Fixed;
            canvas.window = space.window;
            canvas.order = space.order;
            canvas.extra_stylesheets = space.stylesheets;
            canvas.data_context = chrome.vm;
            chrome.canvas = spawn_canvas(world, std::move(canvas), build_dock_chrome_document());
            return chrome;
        }

        void ensure_chrome(ecs::World &world, const DockSpace &space, DockChromeCanvas &chrome) {
            if (!world.valid(chrome.canvas) || world.try_get<UiCanvas>(chrome.canvas) == nullptr) {
                chrome = spawn_chrome(world, space);
            }
        }

        void destroy_chrome(ecs::World &world, const DockChromeCanvas &chrome) {
            if (world.valid(chrome.canvas)) {
                world.destroy(chrome.canvas);
            }
        }

        void place_chrome(ecs::World &world, const DockSpace &space, const DockChromeCanvas &chrome, WindowId window,
                          render::Rect rect, int order, const DockChromeContent &content) {
            UiCanvas &canvas = world.get<UiCanvas>(chrome.canvas);
            canvas.fit = UiFit::Fixed;
            if (canvas.window != window) {
                release_canvas(world, chrome.canvas);
                canvas.window = window;
            }
            canvas.rect = has_area(rect) ? rect : render::Rect{};
            canvas.order = order;
            if (canvas.extra_stylesheets != space.stylesheets) {
                canvas.extra_stylesheets = space.stylesheets;
            }
            fill_dock_chrome(*chrome.vm, content, glm::vec2{rect.x, rect.y});
        }

        // Stacks, strips, tabs and splitters of one tree: the docked one, or a float's.
        void add_tree(const DockSpace &space, const DockGeometry &geometry, DockNodeId float_id,
                      DockChromeContent &content) {
            for (const DockStackRect &stack: geometry.stacks) {
                if (stack.float_id != float_id) {
                    continue;
                }
                content.stacks.push_back(DockChromeBox{stack.rect});
                content.strips.push_back(DockChromeBox{stack.strip});
                for (const DockTabRect &tab: stack.tabs) {
                    DockChromeBox box{tab.rect, dock_panel_title(space, tab.key), tab.active};
                    const DockPanel *panel = dock_panel(space, tab.key);
                    if (panel != nullptr && panel->closable) {
                        box.close = dock_close_button_rect(tab.rect, space.close_button_size);
                    }
                    content.tabs.push_back(std::move(box));
                }
            }
            for (const DockSplitterRect &splitter: geometry.splitters) {
                if (splitter.float_id == float_id) {
                    content.splitters.push_back(DockChromeBox{splitter.grab});
                }
            }
        }

        // The title a float's bar shows: the active panel of its first stack.
        std::string float_title(const DockSpace &space, const DockGeometry &geometry, DockNodeId float_id) {
            for (const DockStackRect &stack: geometry.stacks) {
                if (stack.float_id != float_id) {
                    continue;
                }
                for (const DockTabRect &tab: stack.tabs) {
                    if (tab.active) {
                        return dock_panel_title(space, tab.key);
                    }
                }
            }
            return {};
        }

        void layout_space(ecs::World &world, ecs::Entity entity, DockSpaceRuntime &runtime,
                          const EngineSystemDeps &deps) {
            sync_dock_float_windows(world, entity, runtime, deps);
            {
                const DockSpace &space = world.get<DockSpace>(entity);
                ensure_chrome(world, space, runtime.docked);
                ensure_chrome(world, space, runtime.preview);
                for (const DockFloat &f: space.layout.floats()) {
                    ensure_chrome(world, space, runtime.floats[f.id]);
                }
            }
            const DockSpace &space = world.get<DockSpace>(entity);
            for (auto it = runtime.floats.begin(); it != runtime.floats.end();) {
                if (space.layout.find_float(it->first) == nullptr) {
                    destroy_chrome(world, it->second);
                    it = runtime.floats.erase(it);
                } else {
                    ++it;
                }
            }

            measure_dock_tabs(world, space, runtime);
            const DockGeometry geometry = dock_space_geometry(space, runtime);
            // The geometry of each float in an OS window, in that window.
            std::map<DockNodeId, DockGeometry> windowed;
            for (const auto &[id, window]: runtime.windows) {
                windowed.emplace(id, dock_float_window_geometry(space, runtime, id));
            }

            DockChromeContent docked;
            add_tree(space, geometry, kNoDockNode, docked);
            place_chrome(world, space, runtime.docked, space.window, space.area, dock_docked_chrome_order(space),
                         docked);

            const std::span<const DockFloat> floats = space.layout.floats();
            for (std::size_t z = 0; z < floats.size(); ++z) {
                const DockNodeId id = floats[z].id;
                const DockFloatHome home = dock_float_home(space, runtime, id, floats[z].rect);
                DockChromeContent content;
                if (home.os_window) {
                    // The OS window's own frame and title bar stand in for the virtual ones.
                    add_tree(space, windowed.at(id), id, content);
                } else if (const DockFloatRect *f = geometry.floating(id)) {
                    content.frames.push_back(DockChromeBox{f->frame});
                    content.titles.push_back(DockChromeBox{f->title, float_title(space, geometry, id)});
                    add_tree(space, geometry, id, content);
                }
                place_chrome(world, space, runtime.floats.at(id), home.window, home.rect,
                             dock_float_chrome_order(space, z), content);
            }

            DockChromeContent preview;
            render::Rect preview_rect{};
            WindowId preview_window = space.window;
            if (runtime.gesture.drop && runtime.gesture.preview_window) {
                preview_rect = runtime.gesture.preview_rect;
                preview_window = *runtime.gesture.preview_window;
                preview.previews.push_back(DockChromeBox{preview_rect});
            }
            place_chrome(world, space, runtime.preview, preview_window, preview_rect, dock_preview_order(space),
                         preview);

            std::set<ecs::Entity> shown;
            std::unordered_set<std::string_view> seen;
            for (const DockPanel &panel: space.panels) {
                if (!seen.insert(panel.key).second || !world.valid(panel.canvas) ||
                    world.try_get<UiCanvas>(panel.canvas) == nullptr) {
                    continue;
                }
                // The panel's window: its float's OS window, else the space's.
                WindowId window = space.window;
                const DockPanelRect *rect = geometry.panel(panel.key);
                if (const std::optional<DockPanelPlace> place = space.layout.find(panel.key);
                    place && place->float_id != kNoDockNode) {
                    if (const auto it = windowed.find(place->float_id); it != windowed.end()) {
                        window = runtime.windows.at(place->float_id).window;
                        rect = it->second.panel(panel.key);
                    }
                }
                if (world.get<UiCanvas>(panel.canvas).window != window) {
                    // Popups and focus belong to the window it leaves.
                    release_canvas(world, panel.canvas);
                }
                UiCanvas &canvas = world.get<UiCanvas>(panel.canvas);
                canvas.fit = UiFit::Fixed;
                canvas.window = window;
                if (rect != nullptr && rect->shown && has_area(rect->content)) {
                    canvas.rect = rect->content;
                    canvas.order = rect->float_id == kNoDockNode ? dock_docked_panel_order(space)
                                                                 : dock_float_panel_order(space, rect->float_id);
                    shown.insert(panel.canvas);
                } else {
                    canvas.rect = render::Rect{};
                    canvas.order = dock_docked_panel_order(space);
                }
            }
            for (const ecs::Entity was: runtime.shown) {
                if (!shown.contains(was) && world.valid(was)) {
                    release_canvas(world, was);
                }
            }
            runtime.shown = std::move(shown);
        }

    } // namespace

    float dock_close_reserve(float tab_height, float close_size) noexcept {
        return close_size <= 0.0f ? 0.0f : close_size + std::max((tab_height - close_size) * 0.5f, 0.0f);
    }

    render::Rect dock_close_button_rect(const render::Rect &tab, float size) noexcept {
        const float gap = std::max((tab.h - size) * 0.5f, 0.0f);
        if (size <= 0.0f || tab.w < size + 2.0f * gap) {
            return render::Rect{};
        }
        return render::Rect{tab.x + tab.w - gap - size, tab.y + gap, size, size};
    }

    DockFloatHome dock_float_home(const DockSpace &space, const DockSpaceRuntime &runtime, DockNodeId float_id,
                                  render::Rect frame) {
        if (const std::optional<WindowId> window = dock_float_window(runtime, float_id)) {
            return DockFloatHome{*window, render::Rect{0.0f, 0.0f, frame.w, frame.h}, true};
        }
        if (!has_area(space.area)) {
            return DockFloatHome{space.window, frame, false};
        }
        return DockFloatHome{space.window, dock_float_clamped(frame, space.area), false};
    }

    std::optional<WindowId> dock_float_window(const DockSpaceRuntime &runtime, DockNodeId float_id) {
        const auto it = runtime.windows.find(float_id);
        if (it == runtime.windows.end()) {
            return std::nullopt;
        }
        return it->second.window;
    }

    std::optional<WindowId> dock_panel_os_window(ecs::World &world, ecs::Entity space, std::string_view key) {
        const DockSpace *dock = world.try_get<DockSpace>(space);
        if (dock == nullptr) {
            return std::nullopt;
        }
        const std::optional<DockPanelPlace> place = dock->layout.find(key);
        if (!place || place->float_id == kNoDockNode) {
            return std::nullopt;
        }
        const DockRuntime &runtime = world.ctx<DockRuntime>();
        const auto it = runtime.spaces.find(space);
        if (it == runtime.spaces.end()) {
            return std::nullopt;
        }
        return dock_float_window(it->second, place->float_id);
    }

    DockNodeId dock_float_in_window(const DockSpaceRuntime &runtime, WindowId window) {
        for (const auto &[id, entry]: runtime.windows) {
            if (entry.window == window) {
                return id;
            }
        }
        return kNoDockNode;
    }

    bool dock_space_owns_window(const DockSpace &space, const DockSpaceRuntime &runtime, WindowId window) {
        return window == space.window || dock_float_in_window(runtime, window) != kNoDockNode;
    }

    DockMetrics dock_os_float_metrics(DockMetrics metrics) {
        metrics.frame_border = 0.0f;
        metrics.title_bar_height = 0.0f;
        return metrics;
    }

    DockMetrics dock_space_metrics(const DockSpace &space, const DockSpaceRuntime &runtime) {
        DockMetrics metrics = space.metrics;
        if (!metrics.tab_width_for && !runtime.tabs.by_key.empty()) {
            const DockTabWidths *tabs = &runtime.tabs;
            const float fallback = metrics.tab_width;
            metrics.tab_width_for = [tabs, fallback](std::string_view key) {
                const auto it = tabs->by_key.find(key);
                return it == tabs->by_key.end() ? fallback : it->second.width;
            };
        }
        return metrics;
    }

    DockGeometry dock_space_geometry(const DockSpace &space, const DockSpaceRuntime &runtime) {
        const DockMetrics metrics = dock_space_metrics(space, runtime);
        const bool home = std::ranges::all_of(space.layout.floats(), [&](const DockFloat &f) {
            return dock_float_home(space, runtime, f.id, f.rect).rect == f.rect || runtime.windows.contains(f.id);
        });
        DockGeometry geometry;
        if (home) {
            geometry = compute_dock_geometry(space.layout, space.area, metrics);
        } else {
            DockLayout shown = space.layout;
            for (const DockFloat &f: space.layout.floats()) {
                if (!runtime.windows.contains(f.id)) {
                    shown.set_float_rect(f.id, dock_float_home(space, runtime, f.id, f.rect).rect);
                }
            }
            geometry = compute_dock_geometry(shown, space.area, metrics);
        }
        if (!runtime.windows.empty()) {
            keep_floats(geometry, [&](DockNodeId id) { return !runtime.windows.contains(id); });
        }
        return geometry;
    }

    DockGeometry dock_float_window_geometry(const DockSpace &space, const DockSpaceRuntime &runtime,
                                            DockNodeId float_id) {
        const DockFloat *f = space.layout.find_float(float_id);
        if (f == nullptr) {
            return DockGeometry{};
        }
        DockLayout shown = space.layout;
        shown.set_float_rect(float_id, dock_float_home(space, runtime, float_id, f->rect).rect);
        DockGeometry geometry =
                compute_dock_geometry(shown, render::Rect{}, dock_os_float_metrics(dock_space_metrics(space, runtime)));
        keep_floats(geometry, [&](DockNodeId id) { return id == float_id; });
        // Only the float's own tree: nothing in this window is the docked tree or the dock area.
        std::erase_if(geometry.panels, [](const DockPanelRect &p) { return p.float_id == kNoDockNode; });
        std::erase_if(geometry.stacks, [](const DockStackRect &s) { return s.float_id == kNoDockNode; });
        std::erase_if(geometry.splitters, [](const DockSplitterRect &s) { return s.float_id == kNoDockNode; });
        geometry.area = render::Rect{};
        return geometry;
    }

    const DockPanel *dock_panel(const DockSpace &space, std::string_view key) {
        const auto it = std::ranges::find(space.panels, key, &DockPanel::key);
        return it == space.panels.end() ? nullptr : &*it;
    }

    std::string dock_panel_title(const DockSpace &space, std::string_view key) {
        const DockPanel *panel = dock_panel(space, key);
        return panel != nullptr && !panel->title.empty() ? panel->title : std::string(key);
    }

    int dock_float_chrome_order(const DockSpace &space, std::size_t z) {
        return space.order + 2 + 2 * static_cast<int>(z);
    }

    int dock_float_panel_order(const DockSpace &space, DockNodeId float_id) {
        const std::span<const DockFloat> floats = space.layout.floats();
        const auto it = std::ranges::find(floats, float_id, &DockFloat::id);
        return dock_float_chrome_order(space, static_cast<std::size_t>(it - floats.begin())) + 1;
    }

    int dock_preview_order(const DockSpace &space) {
        return dock_float_chrome_order(space, space.layout.floats().size());
    }

    void run_dock_layout(ecs::World &world, const EngineSystemDeps &deps) {
        DockRuntime &runtime = world.ctx<DockRuntime>();
        std::vector<ecs::Entity> entities;
        for (const ecs::Entity entity: world.view<DockSpace>()) {
            entities.push_back(entity);
        }
        for (auto it = runtime.spaces.begin(); it != runtime.spaces.end();) {
            if (std::ranges::find(entities, it->first) == entities.end()) {
                destroy_chrome(world, it->second.docked);
                destroy_chrome(world, it->second.preview);
                for (const auto &[id, chrome]: it->second.floats) {
                    destroy_chrome(world, chrome);
                }
                close_dock_float_windows(it->second, deps);
                it = runtime.spaces.erase(it);
            } else {
                ++it;
            }
        }
        for (const ecs::Entity entity: entities) {
            layout_space(world, entity, runtime.spaces[entity], deps);
        }
    }

} // namespace engine::ui
