#pragma once

#include <engine/ui/command.h>

namespace launcher {

// An ICommand that calls one method of the object that owns the button. Bound once after the owner
// exists; an unbound command does nothing and reports that it cannot execute.
class MethodCommand final : public engine::ui::ICommand {
public:
    template<typename T, void (T::*Execute)(), bool (T::*CanExecute)() const = nullptr>
    void bind_to(T& owner) {
        owner_ = &owner;
        execute_ = [](void* self) { (static_cast<T*>(self)->*Execute)(); };
        if constexpr (CanExecute != nullptr) {
            can_execute_ = [](const void* self) { return (static_cast<const T*>(self)->*CanExecute)(); };
        } else {
            can_execute_ = nullptr;
        }
    }

    [[nodiscard]] bool can_execute() const override {
        if (owner_ == nullptr) {
            return false;
        }
        return can_execute_ == nullptr || can_execute_(owner_);
    }

    void execute() override {
        if (can_execute()) {
            execute_(owner_);
        }
    }

private:
    void* owner_ = nullptr;
    void (*execute_)(void*) = nullptr;
    bool (*can_execute_)(const void*) = nullptr;
};

}
