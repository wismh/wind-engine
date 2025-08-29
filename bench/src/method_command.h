#pragma once

#include <engine/ui/command.h>

namespace bench {

// An ICommand that calls one method of the object that owns the button. Bound once after the owner exists; an
// unbound command does nothing and reports that it cannot execute. The launcher has the same type.
class MethodCommand final : public engine::ui::ICommand {
public:
    template<typename T, void (T::*Execute)()>
    void bind_to(T& owner) {
        owner_ = &owner;
        execute_ = [](void* self) { (static_cast<T*>(self)->*Execute)(); };
    }

    [[nodiscard]] bool can_execute() const override { return owner_ != nullptr; }

    void execute() override {
        if (can_execute()) {
            execute_(owner_);
        }
    }

private:
    void* owner_ = nullptr;
    void (*execute_)(void*) = nullptr;
};

}
