#pragma once

#include <utility>

namespace engine::ui {

template<typename T>
class Bindable {
public:
    Bindable() = default;
    explicit Bindable(T value) : value_(std::move(value)) {}

    void Set(T value) { value_ = std::move(value); }

    [[nodiscard]] const T& Get() const { return value_; }

    Bindable& operator=(T value) {
        Set(std::move(value));
        return *this;
    }

private:
    T value_{};
};

}
