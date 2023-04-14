#pragma once

#include <functional>

namespace engine::ui {

class ICommand {
public:
    virtual ~ICommand() = default;
    [[nodiscard]] virtual bool CanExecute() const = 0;
    virtual void Execute() = 0;
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

    [[nodiscard]] bool CanExecute() const override {
        if (can_execute_fn_) {
            return can_execute_fn_();
        }
        return can_execute_;
    }

    void Execute() override {
        if (execute_ && CanExecute()) {
            execute_();
        }
    }

private:
    std::function<void()> execute_{};
    std::function<bool()> can_execute_fn_{};
    bool can_execute_ = true;
};

}
