#pragma once

#include <utility>
#include <vector>

namespace engine::ui {

template<typename T>
class Bindable {
public:
    Bindable() = default;
    explicit Bindable(T value) : value_(std::move(value)) {}

    void set(T value) { value_ = std::move(value); }

    [[nodiscard]] const T& get() const { return value_; }

    Bindable& operator=(T value) {
        set(std::move(value));
        return *this;
    }

private:
    T value_{};
};

template<typename T>
class BindableList {
public:
    void set(std::vector<T> items) { items_ = std::move(items); }

    [[nodiscard]] const std::vector<T>& get() const { return items_; }

    [[nodiscard]] std::vector<T>& get() { return items_; }

private:
    std::vector<T> items_{};
};

}
