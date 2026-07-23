#pragma once

// docs/tech/features/Docking.md#host — the chrome canvases' document and view-models.

#include <engine/render/commands.h>
#include <engine/ui/bindable.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <vector>

namespace engine::ui {

    // One chrome box: a stack background, a tab strip, a tab, a splitter, a float frame or title, the drop preview.
    // x, y, w, h are relative to the chrome canvas's rect; CSS places `.dock-box` with them.
    class DockChromeItem final : public ViewModel {
    public:
        Bindable<float> x;
        Bindable<float> y;
        Bindable<float> w;
        Bindable<float> h;
        Bindable<std::string> title;
        Bindable<bool> active;
        // `none` hides a tab's close button, `block` shows it.
        Bindable<std::string> close;
        // The close button inside its tab.
        Bindable<float> cx;
        Bindable<float> cy;
        Bindable<float> cs;
        // `--reserve` of a tab: room its close button takes at the right (0 without one). The theme pads with it.
        Bindable<float> reserve;

        DockChromeItem();
    };

    // What one chrome canvas shows. A canvas only fills the lists of its layer.
    class DockChromeViewModel final : public ViewModel {
    public:
        BindableList<std::shared_ptr<DockChromeItem>> frames;
        BindableList<std::shared_ptr<DockChromeItem>> titles;
        BindableList<std::shared_ptr<DockChromeItem>> stacks;
        BindableList<std::shared_ptr<DockChromeItem>> strips;
        BindableList<std::shared_ptr<DockChromeItem>> splitters;
        BindableList<std::shared_ptr<DockChromeItem>> tabs;
        BindableList<std::shared_ptr<DockChromeItem>> previews;

        DockChromeViewModel();
    };

    // A chrome box as the dock system computes it, in window pixels.
    struct DockChromeBox {
        render::Rect rect{};
        std::string title;
        bool active = false;
        // Window rect of a tab's close button; empty when the tab has none.
        render::Rect close{};
    };

    // Lists of one chrome canvas, in window pixels.
    struct DockChromeContent {
        std::vector<DockChromeBox> frames;
        std::vector<DockChromeBox> titles;
        std::vector<DockChromeBox> stacks;
        std::vector<DockChromeBox> strips;
        std::vector<DockChromeBox> splitters;
        std::vector<DockChromeBox> tabs;
        std::vector<DockChromeBox> previews;
    };

    // The same document for every chrome canvas: an ItemsControl per list, painted frames first, previews last.
    [[nodiscard]] UiDocument build_dock_chrome_document();

    // The tab row of the chrome document, generated once under its ancestors (the canvas, the tabs ItemsControl),
    // so a stylesheet resolves `.dock-tab` on it the way it does on the live chrome.
    [[nodiscard]] UiDocument build_dock_tab_probe();
    // The `.dock-tab` Button of build_dock_tab_probe().
    [[nodiscard]] Element &dock_tab_probe_button(UiDocument &probe);

    // Writes `content` into `vm` relative to `origin` (the chrome canvas's rect origin). Keeps the item view-models
    // that stay, so a row's element survives and only changed values re-lay out.
    void fill_dock_chrome(DockChromeViewModel &vm, const DockChromeContent &content, glm::vec2 origin);

} // namespace engine::ui
