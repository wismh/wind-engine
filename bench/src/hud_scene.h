#pragma once

#include "bench_case.h"
#include "hud_view_model.h"
#include "scene.h"

#include <memory>
#include <string_view>

namespace bench {

// The strategy HUD. The simulation is fake: the resource numbers are fixed, and one-change adds one to the gold every
// frame. The alert pulses in every mode (an infinite @keyframes); the camera never moves.
class HudScene final : public IScene {
public:
    explicit HudScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

    // The order buttons. Nothing clicks them in a run; they make the buttons real commands.
    void order_move();
    void order_attack();
    void order_hold();
    void order_patrol();
    void order_build();
    void order_stop();

private:
    void give_order(std::string_view order);

    BenchCase case_;
    std::shared_ptr<HudViewModel> view_model_;
    int gold_ = 0;
    std::string_view last_order_;
};

}
