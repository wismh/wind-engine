#pragma once

// docs/tech/modules/UI.md

#include <functional>

namespace engine::ui {

class ICommand {
public:
    virtual ~ICommand() = default;
    [[nodiscard]] virtual bool can_execute() const = 0;
    virtual void execute() = 0;
};

class RelayCommand final : public ICommand {
public:
    RelayCommand() = default;

    explicit RelayCommand(std::function<void()> execute, std::function<bool()> can_execute = {})
        : execute_(std::move(execute)), can_execute_fn_(std::move(can_execute)) {}

    RelayCommand& operator=(std::function<void()> execute) {
        execute_ = std::move(execute);
        return *this;
    }

    void set_can_execute(bool value) { can_execute_ = value; }

    [[nodiscard]] bool can_execute() const override {
        if (can_execute_fn_) {
            return can_execute_fn_();
        }
        return can_execute_;
    }

    void execute() override {
        if (execute_ && can_execute()) {
            execute_();
        }
    }

private:
    std::function<void()> execute_{};
    std::function<bool()> can_execute_fn_{};
    bool can_execute_ = true;
};

// An ICommand that calls one method of the object that owns the button: a pointer to the owner and a plain function
// pointer, no std::function and no allocation. The view model exists before its owner, so the owner is bound later with
// bind_to. An unbound command does nothing and reports that it cannot execute. Binding again replaces the owner.
class MethodCommand final : public ICommand {
public:
    // CanExecute is an optional const method of the owner; without it the command can execute whenever it is bound.
    template<typename Owner, void (Owner::*Execute)(), bool (Owner::*CanExecute)() const = nullptr>
    void bind_to(Owner& owner) {
        owner_ = &owner;
        execute_ = [](void* self) { (static_cast<Owner*>(self)->*Execute)(); };
        if constexpr (CanExecute != nullptr) {
            can_execute_ = [](const void* self) { return (static_cast<const Owner*>(self)->*CanExecute)(); };
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
