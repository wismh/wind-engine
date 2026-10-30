#include "inspector_field_view_model.h"

#include "inspector_panel.h"

#include <asset_ids.h>

namespace editor {
namespace {

const char* shown(bool visible) {
    return visible ? "flex" : "none";
}

}

InspectorFieldViewModel::InspectorFieldViewModel(InspectorPanel& panel) : panel_(&panel) {
    assets::ui::Inspector::Sections::Fields::bind(*this);
    chooseAsset.bind_to<InspectorFieldViewModel, &InspectorFieldViewModel::on_choose_asset>(*this);
    clearAsset.bind_to<InspectorFieldViewModel, &InspectorFieldViewModel::on_clear_asset>(*this);
    value.on_changed([this](const std::string& text) { on_value(text); });
    x.on_changed([this](const std::string& text) { on_axis(0, text); });
    y.on_changed([this](const std::string& text) { on_axis(1, text); });
    z.on_changed([this](const std::string& text) { on_axis(2, text); });
    w.on_changed([this](const std::string& text) { on_axis(3, text); });
    checked.on_changed([this](const bool& value) { on_checked(value); });
}

void InspectorFieldViewModel::show(
        const std::string& component, const InspectorField& field, std::string_view editing_key) {
    component_ = component;
    key_ = field.key;
    axis_integer_ = field.axis_integer;
    const bool text_row = field.kind == InspectorFieldKind::Text || !field.editable;
    nameDisplay = std::string(shown(!text_row));
    label = text_row ? field.text : field.key;
    textDisplay = std::string(shown(text_row));
    if (editing_key != key_for(-1)) {
        text = field.text;
        value = field.kind == InspectorFieldKind::String || field.kind == InspectorFieldKind::Number ? field.text
                                                                                                      : std::string{};
    }
    checkDisplay = std::string(shown(field.editable && field.kind == InspectorFieldKind::Bool));
    if (editing_key != key_for(-1)) {
        checked = field.flag;
    }
    inputDisplay = std::string(
            shown(field.editable &&
                    (field.kind == InspectorFieldKind::String || field.kind == InspectorFieldKind::Number)));
    const bool vec = field.editable && field.kind == InspectorFieldKind::Vec;
    xDisplay = std::string(shown(vec && field.axis_count > 0));
    yDisplay = std::string(shown(vec && field.axis_count > 1));
    zDisplay = std::string(shown(vec && field.axis_count > 2));
    wDisplay = std::string(shown(vec && field.axis_count > 3));
    if (vec) {
        if (editing_key != key_for(0)) {
            x = field.axes[0];
        }
        if (editing_key != key_for(1)) {
            y = field.axes[1];
        }
        if (editing_key != key_for(2)) {
            z = field.axes[2];
        }
        if (editing_key != key_for(3)) {
            w = field.axes[3];
        }
    }
    const bool asset_row = field.editable && field.kind == InspectorFieldKind::Asset;
    assetDisplay = std::string(shown(asset_row));
    if (asset_row) {
        assetName = field.asset_name.empty() ? std::string("(None)") : field.asset_name;
        asset_filter_ = field.asset_filter;
    }
}

void InspectorFieldViewModel::on_choose_asset() {
    if (panel_->refreshing()) {
        return;
    }
    panel_->choose_asset(component_, key_, asset_filter_);
}

void InspectorFieldViewModel::on_clear_asset() {
    if (panel_->refreshing()) {
        return;
    }
    panel_->clear_asset(component_, key_);
}

void InspectorFieldViewModel::on_value(const std::string& next) {
    if (panel_->refreshing()) {
        return;
    }
    panel_->edit_text(component_, key_, -1, next);
}

void InspectorFieldViewModel::on_axis(int slot, const std::string& next) {
    if (panel_->refreshing()) {
        return;
    }
    panel_->edit_text(component_, key_, slot, next);
}

void InspectorFieldViewModel::on_checked(bool next) {
    if (panel_->refreshing()) {
        return;
    }
    panel_->edit_bool(component_, key_, next);
}

std::string InspectorFieldViewModel::key_for(int slot) const {
    return component_ + "\n" + key_ + "\n" + std::to_string(slot);
}

InspectorAddViewModel::InspectorAddViewModel(InspectorPanel& panel) : panel_(&panel) {
    assets::ui::Inspector::Addables::bind(*this);
    add.bind_to<InspectorAddViewModel, &InspectorAddViewModel::add_component>(*this);
}

void InspectorAddViewModel::show(std::string component_name) {
    name_ = std::move(component_name);
    name = name_;
}

void InspectorAddViewModel::add_component() {
    panel_->add_component(name_);
}

}
