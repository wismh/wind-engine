#pragma once

// docs/tech/modules/UI.md

#include <concepts>
#include <functional>
#include <utility>
#include <vector>

namespace engine::ui {

template<typename T>
class Bindable {
public:
    Bindable() = default;
    explicit Bindable(T value) : value_(std::move(value)) {}

    // Stores the value. When it differs from the current one (`==`; a type without `==` always counts as changed) the
    // change handler runs once, synchronously, with the new value. A write from C++ and a write from the document
    // (drag, TextInput, Checkbox) take this same path, so both are reported and an unchanged value is not. The
    // handler runs on the main thread inside `set` (inside input handling for a document write): do not destroy the
    // view model or its canvas from it.
    void set(T value) {
        if constexpr (std::equality_comparable<T>) {
            if (value_ == value) {
                return;
            }
        }
        value_ = std::move(value);
        if (on_changed_) {
            on_changed_(value_);
        }
    }

    // One handler per bindable; a new one replaces the old, an empty one removes it.
    void on_changed(std::function<void(const T&)> handler) { on_changed_ = std::move(handler); }

    [[nodiscard]] const T& get() const { return value_; }

    Bindable& operator=(T value) {
        set(std::move(value));
        return *this;
    }

private:
    T value_{};
    std::function<void(const T&)> on_changed_{};
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
