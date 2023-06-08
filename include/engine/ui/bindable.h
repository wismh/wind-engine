#pragma once

#include <utility>
#include <vector>

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

template<typename T>
class BindableList {
public:
    void Set(std::vector<T> items) { items_ = std::move(items); }

    [[nodiscard]] const std::vector<T>& Get() const { return items_; }

    [[nodiscard]] std::vector<T>& Get() { return items_; }

private:
    std::vector<T> items_{};
};

}
