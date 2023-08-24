#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ui {

class ViewModel {
public:
    ViewModel() = default;
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&) = delete;
    ViewModel& operator=(const ViewModel&) = delete;
    ViewModel(ViewModel&&) = delete;
    ViewModel& operator=(ViewModel&&) = delete;

    [[nodiscard]] bool has_property(std::string_view name) const;
    [[nodiscard]] bool has_command(std::string_view name) const;
    [[nodiscard]] std::optional<std::string> read_property_string(std::string_view name) const;
    [[nodiscard]] std::vector<ViewModel*> read_item_source(std::string_view name) const;
    [[nodiscard]] ICommand* find_command(std::string_view name);
    [[nodiscard]] const ICommand* find_command(std::string_view name) const;

protected:
    template<typename T>
    void property(std::string_view name, Bindable<T>& bindable);

    template<typename T>
    void property(std::string_view name, BindableList<std::shared_ptr<T>>& list);

    void command(std::string_view name, ICommand& command);

private:
    struct PropertyRef {
        void* bindable = nullptr;
        std::string (*to_string)(void*) = nullptr;
        std::vector<ViewModel*> (*items)(void*) = nullptr;
    };

    std::unordered_map<std::string, PropertyRef> properties_;
    std::unordered_map<std::string, ICommand*> commands_;
};

template<typename T>
void ViewModel::property(std::string_view name, Bindable<T>& bindable) {
    PropertyRef ref;
    ref.bindable = &bindable;
    ref.to_string = [](void* ptr) -> std::string {
        const T& value = static_cast<Bindable<T>*>(ptr)->get();
        if constexpr (std::is_same_v<T, std::string>) {
            return value;
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value);
        } else {
            return {};
        }
    };
    properties_.insert_or_assign(std::string(name), ref);
}

template<typename T>
void ViewModel::property(std::string_view name, BindableList<std::shared_ptr<T>>& list) {
    static_assert(std::is_base_of_v<ViewModel, T>, "items_source items must be ViewModels");
    PropertyRef ref;
    ref.bindable = &list;
    ref.items = [](void* ptr) -> std::vector<ViewModel*> {
        auto& items = static_cast<BindableList<std::shared_ptr<T>>*>(ptr)->get();
        std::vector<ViewModel*> out;
        out.reserve(items.size());
        for (const std::shared_ptr<T>& item : items) {
            if (item) {
                out.push_back(item.get());
            }
        }
        return out;
    };
    properties_.insert_or_assign(std::string(name), ref);
}

}
