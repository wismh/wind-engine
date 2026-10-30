#pragma once

#include "inspector_section.h"

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <array>
#include <string>

namespace editor {

class InspectorPanel;

// One editable row of a component section (`fields` in assets/ui/inspector.xml). Hidden widgets stay in the
// template; their display variable is `none`.
class InspectorFieldViewModel final : public engine::ui::ViewModel {
public:
    explicit InspectorFieldViewModel(InspectorPanel& panel);

    InspectorFieldViewModel(const InspectorFieldViewModel&) = delete;
    InspectorFieldViewModel& operator=(const InspectorFieldViewModel&) = delete;

    // `editing_key` is the field the user is typing. That widget is left alone so a half-typed number is not
    // replaced by the formatted value.
    void show(const std::string& component, const InspectorField& field, std::string_view editing_key);

    engine::ui::Bindable<std::string> label;
    engine::ui::Bindable<std::string> nameDisplay{std::string("flex")};
    engine::ui::Bindable<std::string> text;
    engine::ui::Bindable<std::string> textDisplay{std::string("none")};
    engine::ui::Bindable<std::string> checkDisplay{std::string("none")};
    engine::ui::Bindable<bool> checked{false};
    engine::ui::Bindable<std::string> inputDisplay{std::string("none")};
    engine::ui::Bindable<std::string> value;
    engine::ui::Bindable<std::string> xDisplay{std::string("none")};
    engine::ui::Bindable<std::string> x;
    engine::ui::Bindable<std::string> yDisplay{std::string("none")};
    engine::ui::Bindable<std::string> y;
    engine::ui::Bindable<std::string> zDisplay{std::string("none")};
    engine::ui::Bindable<std::string> z;
    engine::ui::Bindable<std::string> wDisplay{std::string("none")};
    engine::ui::Bindable<std::string> w;
    engine::ui::Bindable<std::string> assetDisplay{std::string("none")};
    engine::ui::Bindable<std::string> assetName{std::string("(None)")};
    engine::ui::MethodCommand chooseAsset;
    engine::ui::MethodCommand clearAsset;

private:
    void on_choose_asset();
    void on_clear_asset();
    void on_value(const std::string& next);
    void on_axis(int slot, const std::string& next);
    void on_checked(bool next);
    [[nodiscard]] std::string key_for(int slot) const;

    InspectorPanel* panel_;
    std::string component_;
    std::string key_;
    std::array<bool, 4> axis_integer_{};
    engine::AssetFilter asset_filter_{};
};

// One name in the Add Component list.
class InspectorAddViewModel final : public engine::ui::ViewModel {
public:
    explicit InspectorAddViewModel(InspectorPanel& panel);

    InspectorAddViewModel(const InspectorAddViewModel&) = delete;
    InspectorAddViewModel& operator=(const InspectorAddViewModel&) = delete;

    void show(std::string name);

    void add_component();

    engine::ui::Bindable<std::string> name;
    engine::ui::MethodCommand add;

private:
    InspectorPanel* panel_;
    std::string name_;
};

}
