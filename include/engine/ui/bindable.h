#pragma once

// docs/tech/modules/UI.md

#include <concepts>
#include <cstdint>
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
    // version goes up by one and the change handler runs once, synchronously, with the new value. A write from C++
    // and a write from the document (drag, TextInput, Checkbox) take this same path, so both are reported and an
    // unchanged value is not. The handler runs on the main thread inside `set` (inside input handling for a document
    // write): do not destroy the view model or its canvas from it.
    void set(T value) {
        if constexpr (std::equality_comparable<T>) {
            if (value_ == value) {
                return;
            }
        }
        value_ = std::move(value);
        ++version_;
        if (on_changed_) {
            on_changed_(value_);
        }
    }

    // One handler per bindable; a new one replaces the old, an empty one removes it.
    void on_changed(std::function<void(const T&)> handler) { on_changed_ = std::move(handler); }

    [[nodiscard]] const T& get() const { return value_; }

    // Counts the changes `set` accepted, from 0. The UI bind pass polls it: a document slot remembers the version it
    // last read and reads the value again only when this number moved. It is not a time and not an id: compare it for
    // equality with what you saw before.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }

    Bindable& operator=(T value) {
        set(std::move(value));
        return *this;
    }

private:
    T value_{};
    std::uint64_t version_ = 0;
    std::function<void(const T&)> on_changed_{};
};

// A list of items for an `ItemsControl`. It changes through `set` and `edit`, both of which raise the version only
// when the list really differs afterwards (the items compare with `==`; an element type without `==` always counts as
// changed). There is no mutable `get()`: a change the list does not see could not be polled.
template<typename T>
class BindableList {
public:
    // Replaces the items. Equal items change nothing.
    void set(std::vector<T> items) {
        if constexpr (std::equality_comparable<T>) {
            if (items_ == items) {
                return;
            }
        }
        items_ = std::move(items);
        ++version_;
    }

    // Runs `change(std::vector<T>&)` on the items. The version goes up once when the items differ afterwards
    // (compared with a copy taken before, so an edit costs one copy of the list). Do not keep a reference into the
    // vector past the call.
    template<typename Change>
    void edit(Change &&change) {
        if constexpr (std::equality_comparable<T>) {
            const std::vector<T> before = items_;
            std::forward<Change>(change)(items_);
            if (items_ != before) {
                ++version_;
            }
        } else {
            std::forward<Change>(change)(items_);
            ++version_;
        }
    }

    [[nodiscard]] const std::vector<T>& get() const { return items_; }

    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }

private:
    std::vector<T> items_{};
    std::uint64_t version_ = 0;
};

}
