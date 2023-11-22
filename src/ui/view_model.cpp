#include <engine/ui/view_model.h>

namespace engine::ui {

bool ViewModel::has_property(BindingId id) const {
    return properties_.contains(id);
}

bool ViewModel::has_command(BindingId id) const {
    return commands_.contains(id);
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

}
