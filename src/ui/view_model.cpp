#include <engine/ui/view_model.h>

namespace engine::ui {

bool ViewModel::has_property(std::string_view name) const {
    return properties_.contains(std::string(name));
}

bool ViewModel::has_command(std::string_view name) const {
    return commands_.contains(std::string(name));
}

std::optional<std::string> ViewModel::read_property_string(std::string_view name) const {
    const auto it = properties_.find(std::string(name));
    if (it == properties_.end() || it->second.to_string == nullptr) {
        return std::nullopt;
    }
    return it->second.to_string(it->second.bindable);
}

ICommand* ViewModel::find_command(std::string_view name) {
    const auto it = commands_.find(std::string(name));
    if (it == commands_.end()) {
        return nullptr;
    }
    return it->second;
}

const ICommand* ViewModel::find_command(std::string_view name) const {
    const auto it = commands_.find(std::string(name));
    if (it == commands_.end()) {
        return nullptr;
    }
    return it->second;
}

void ViewModel::Command(std::string_view name, ICommand& command) {
    commands_.insert_or_assign(std::string(name), &command);
}

}
