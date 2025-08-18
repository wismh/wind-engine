#include "ui/dock_runtime.h"

#include <engine/ui/canvas.h>
#include <engine/ui/document.h>

#include "ui/painter.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace engine::ui {

    namespace {

        // Font and horizontal padding of `.dock-tab` as the chrome stylesheet resolves it.
        struct TabStyle {
            AssetId font{};
            float font_size = kDefaultFontSize;
            float padding = 0.0f;
        };

        TabStyle resolve_tab_style(const Stylesheet &sheet, float reserve, glm::vec2 window) {
            UiDocument probe = build_dock_tab_probe();
            Element &button = dock_tab_probe_button(probe);
            if (reserve > 0.0f) {
                button.custom_properties["reserve"] = std::to_string(reserve);
            }
            apply_layout_style(probe.root, &sheet, window.x, window.y);
            TabStyle style;
            style.font = button.font_family;
            style.font_size = resolve_font_size(button.font_size, kDefaultFontSize);
            style.padding = resolve_length(button.padding.left, 0.0f, style.font_size) +
                            resolve_length(button.padding.right, 0.0f, style.font_size);
            return style;
        }

    } // namespace

    void measure_dock_tabs(ecs::World &world, const DockSpace &space, DockSpaceRuntime &runtime) {
        DockTabWidths &tabs = runtime.tabs;
        IUiPainter *painter = layout_painter_for(world, space.window);
        const UiInstance *chrome =
                world.valid(runtime.docked.canvas) ? world.try_get<UiInstance>(runtime.docked.canvas) : nullptr;
        if (painter == nullptr || chrome == nullptr || !chrome->stylesheet.has_value()) {
            tabs = DockTabWidths{};
            return;
        }
        const Stylesheet &sheet = *chrome->stylesheet;
        const WindowSize size = window_size_for(world, space.window);
        const glm::vec2 window{static_cast<float>(size.width), static_cast<float>(size.height)};
        const float reserve = dock_close_reserve(space.metrics.tab_strip_height, space.close_button_size);
        if (tabs.painter != painter || tabs.sheet != &sheet || tabs.sheet_generation != sheet.generation ||
            tabs.reserve != reserve || tabs.window != window) {
            tabs = DockTabWidths{.painter = painter,
                                 .sheet = &sheet,
                                 .sheet_generation = sheet.generation,
                                 .reserve = reserve,
                                 .window = window};
        }

        // Styles are resolved only when a tab needs measuring.
        std::optional<TabStyle> plain;
        std::optional<TabStyle> closable;
        std::map<std::string, DockTabWidths::Tab, std::less<>> measured;
        for (const DockPanel &panel: space.panels) {
            if (measured.contains(panel.key)) {
                continue;
            }
            std::string title = dock_panel_title(space, panel.key);
            if (const auto it = tabs.by_key.find(panel.key);
                it != tabs.by_key.end() && it->second.title == title && it->second.closable == panel.closable) {
                measured.emplace(panel.key, std::move(it->second));
                continue;
            }
            std::optional<TabStyle> &style = panel.closable ? closable : plain;
            if (!style) {
                style = resolve_tab_style(sheet, panel.closable ? reserve : 0.0f, window);
            }
            const float text = painter->measure_text(title, style->font, style->font_size).x;
            const float width = std::ceil(text + style->padding);
            measured.emplace(panel.key, DockTabWidths::Tab{std::move(title), panel.closable, width});
        }
        tabs.by_key = std::move(measured);
    }

} // namespace engine::ui
