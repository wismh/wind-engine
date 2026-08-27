#include "hud_scene.h"

#include "bench_assets.h"
#include "bench_random.h"
#include "hud_data.h"

#include <asset_ids.h>

#include <array>
#include <format>

namespace bench {
namespace {

constexpr int kUnitRows = 36;
constexpr int kStartGold = 12450;

constexpr std::array<std::string_view, 6> kUnitKinds{"Archer", "Knight", "Pikeman", "Scout", "Mage", "Healer"};
constexpr std::array<std::string_view, 5> kTasks{"Idle", "Moving", "Gathering", "Guarding", "Fighting"};

}

HudScene::HudScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<HudViewModel>(make_unit_markers(kWorldUnits, kWorldSize)))
    , gold_(kStartGold) {
    HudViewModel& vm = *view_model_;
    vm.gold = std::to_string(gold_);
    vm.wood = std::string("8320");
    vm.stone = std::string("4105");
    vm.food = std::string("15790");
    vm.iron = std::string("2260");
    vm.mana = std::string("640");
    vm.population = std::format("{}/{}", kWorldUnits / 4, 150);
    vm.day = std::string("17");
    vm.alertText = std::string("Under attack!");

    BenchRandom random(kBenchSeed + 3);
    std::vector<std::shared_ptr<UnitRowViewModel>> rows;
    for (int i = 0; i < kUnitRows; ++i) {
        const std::string_view kind = kUnitKinds[static_cast<std::size_t>(i) % kUnitKinds.size()];
        const std::string_view task = kTasks[static_cast<std::size_t>(random.between(0, 4))];
        rows.push_back(std::make_shared<UnitRowViewModel>(std::format("{} {}", kind, i + 1),
                std::format("{}/100", random.between(10, 100)), std::string(task)));
    }
    vm.units.set(std::move(rows));

    vm.selectedKind = std::string("K");
    vm.selectedName = std::string("Knight 2");
    vm.selectedHp = std::string("Health 86/100");
    vm.selectedAttack = std::string("Attack 14");
    vm.selectedArmor = std::string("Armor 6");
    vm.selectedSpeed = std::string("Speed 3.5");

    vm.move.bind_to<HudScene, &HudScene::order_move>(*this);
    vm.attack.bind_to<HudScene, &HudScene::order_attack>(*this);
    vm.hold.bind_to<HudScene, &HudScene::order_hold>(*this);
    vm.patrol.bind_to<HudScene, &HudScene::order_patrol>(*this);
    vm.build.bind_to<HudScene, &HudScene::order_build>(*this);
    vm.stop.bind_to<HudScene, &HudScene::order_stop>(*this);
}

engine::AssetId HudScene::document() const {
    return assets::ui::hud;
}

std::vector<engine::AssetId> HudScene::stylesheets() const {
    return {assets::css::hud, hover_stylesheet(case_)};
}

std::shared_ptr<engine::ui::ViewModel> HudScene::view_model() const {
    return view_model_;
}

void HudScene::change_value(int step) {
    view_model_->gold = std::to_string(gold_ + step + 1);
}

void HudScene::churn(int) {}

void HudScene::order_move() {
    give_order("move");
}

void HudScene::order_attack() {
    give_order("attack");
}

void HudScene::order_hold() {
    give_order("hold");
}

void HudScene::order_patrol() {
    give_order("patrol");
}

void HudScene::order_build() {
    give_order("build");
}

void HudScene::order_stop() {
    give_order("stop");
}

void HudScene::give_order(std::string_view order) {
    last_order_ = order;
}

}
