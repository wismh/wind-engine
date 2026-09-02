#pragma once

#include <engine/resources/asset_id.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <vector>

namespace bench {

// One bench scene: the document, its stylesheets for the case's variant, the view-model it binds, and what the
// modes change on it. Data is generated once, in the constructor, from kBenchSeed.
class IScene {
public:
    virtual ~IScene() = default;

    [[nodiscard]] virtual engine::AssetId document() const = 0;
    // The first is UiCanvas::stylesheet, the rest UiCanvas::extra_stylesheets.
    [[nodiscard]] virtual std::vector<engine::AssetId> stylesheets() const = 0;
    [[nodiscard]] virtual std::shared_ptr<engine::ui::ViewModel> view_model() const = 0;

    // One-change: the scene's one live value takes its value for `step`. Nothing else is written.
    virtual void change_value(int step) = 0;
    // Churn: the scene's structural edit for `step`.
    virtual void churn(int step) = 0;
};

}
