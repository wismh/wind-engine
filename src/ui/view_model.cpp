#include <engine/ui/view_model.h>

namespace engine::ui {

bool ViewModel::has_property(BindingId id) const {
    return properties_.contains(id);
}

bool ViewModel::has_command(BindingId id) const {
    return commands_.contains(id);
}

bool ViewModel::has_paint(BindingId id) const {
    return paints_.contains(id);
}

std::optional<std::string> ViewModel::read_property_string(BindingId id) const {
    const auto it = properties_.find(id);
    if (it == properties_.end() || it->second.to_string == nullptr) {
        return std::nullopt;
    }
    return it->second.to_string(it->second.bindable);
}

std::optional<AssetId> ViewModel::read_property_asset_id(BindingId id) const {
    const auto it = properties_.find(id);
    if (it == properties_.end() || it->second.read_asset_id == nullptr) {
        return std::nullopt;
    }
    return it->second.read_asset_id(it->second.bindable);
}

std::optional<float> ViewModel::read_property_float(BindingId id) const {
    const auto it = properties_.find(id);
    if (it == properties_.end() || it->second.read_float == nullptr) {
        return std::nullopt;
    }
    return it->second.read_float(it->second.bindable);
}

bool ViewModel::write_property_float(BindingId id, float value) {
    const auto it = properties_.find(id);
    if (it == properties_.end() || it->second.write_float == nullptr) {
        return false;
    }
    it->second.write_float(it->second.bindable, value);
    return true;
}

std::vector<ViewModel*> ViewModel::read_item_source(BindingId id) const {
    const auto it = properties_.find(id);
    if (it == properties_.end() || it->second.items == nullptr) {
        return {};
    }
    return it->second.items(it->second.bindable);
}

ICommand* ViewModel::find_command(BindingId id) {
    const auto it = commands_.find(id);
    if (it == commands_.end()) {
        return nullptr;
    }
    return it->second;
}

const ICommand* ViewModel::find_command(BindingId id) const {
    const auto it = commands_.find(id);
    if (it == commands_.end()) {
        return nullptr;
    }
    return it->second;
}

void ViewModel::command(BindingId id, ICommand& command) {
    commands_.insert_or_assign(id, &command);
}

IPaint* ViewModel::find_paint(BindingId id) {
    const auto it = paints_.find(id);
    if (it == paints_.end()) {
        return nullptr;
    }
    return it->second;
}

const IPaint* ViewModel::find_paint(BindingId id) const {
    const auto it = paints_.find(id);
    if (it == paints_.end()) {
        return nullptr;
    }
    return it->second;
}

void ViewModel::paint(BindingId id, IPaint& paint) {
    paints_.insert_or_assign(id, &paint);
}

}
