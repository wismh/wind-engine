#pragma once

#include "method_command.h"
#include "minimap_paint.h"
#include "unit_marker.h"
#include "unit_row_view_model.h"
#include "world_paint.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <vector>

namespace bench {

// Fields of assets/ui/hud.xml. The order buttons are bound by HudScene, which owns what they change.
class HudViewModel final : public engine::ui::ViewModel {
public:
    explicit HudViewModel(const std::vector<UnitMarker>& units);

    // The resource bar. `gold` is the value the one-change mode writes.
    engine::ui::Bindable<std::string> gold;
    engine::ui::Bindable<std::string> wood;
    engine::ui::Bindable<std::string> stone;
    engine::ui::Bindable<std::string> food;
    engine::ui::Bindable<std::string> iron;
    engine::ui::Bindable<std::string> mana;
    engine::ui::Bindable<std::string> population;
    engine::ui::Bindable<std::string> day;
    engine::ui::Bindable<std::string> alertText;

    engine::ui::BindableList<std::shared_ptr<UnitRowViewModel>> units;

    engine::ui::Bindable<std::string> selectedKind;
    engine::ui::Bindable<std::string> selectedName;
    engine::ui::Bindable<std::string> selectedHp;
    engine::ui::Bindable<std::string> selectedAttack;
    engine::ui::Bindable<std::string> selectedArmor;
    engine::ui::Bindable<std::string> selectedSpeed;

    // The Viewport camera. Still in every mode.
    engine::ui::Bindable<float> panX;
    engine::ui::Bindable<float> panY;
    engine::ui::Bindable<float> zoom{1.0f};

    WorldPaint world;
    MinimapPaint minimap;

    MethodCommand move;
    MethodCommand attack;
    MethodCommand hold;
    MethodCommand patrol;
    MethodCommand build;
    MethodCommand stop;
};

}
