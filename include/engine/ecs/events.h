#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::ecs {

class World;

template<typename T>
class EventReader;

template<typename T>
class Events {
    friend class EventReader<T>;

public:
    void send(T event) {
        current_.push_back(std::move(event));
    }

    void update() {
        previous_.clear();
        previous_.swap(current_);
    }

private:
    std::vector<T> previous_;
    std::vector<T> current_;
};

template<typename T>
struct IsEvents : std::false_type {};

template<typename U>
struct IsEvents<Events<U>> : std::true_type {};

template<typename T>
class EventWriter {
public:
    explicit EventWriter(Events<T>& events) : events_(&events) {}
    explicit EventWriter(World& world);

    void send(T event) {
        events_->send(std::move(event));
    }

private:
    Events<T>* events_ = nullptr;
};

template<typename T>
class EventReader {
public:
    explicit EventReader(const Events<T>& events) : events_(&events) {}
    explicit EventReader(World& world);

    class Iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;
        using iterator_category = std::forward_iterator_tag;

        reference operator*() const {
            if (index_ < previous_size_) {
                return previous_[index_];
            }
            return current_[index_ - previous_size_];
        }

        pointer operator->() const {
            return &**this;
        }

        Iterator& operator++() {
            ++index_;
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept {
            return index_ == other.index_;
        }

    private:
        friend class EventReader;

        Iterator(const T* previous, std::size_t previous_size, const T* current, std::size_t index)
            : previous_(previous)
            , previous_size_(previous_size)
            , current_(current)
            , index_(index) {}

        const T* previous_ = nullptr;
        std::size_t previous_size_ = 0;
        const T* current_ = nullptr;
        std::size_t index_ = 0;
    };

    [[nodiscard]] Iterator begin() const {
        return Iterator(events_->previous_.data(), events_->previous_.size(), events_->current_.data(), 0);
    }

    [[nodiscard]] Iterator end() const {
        const std::size_t total = events_->previous_.size() + events_->current_.size();
        return Iterator(events_->previous_.data(), events_->previous_.size(), events_->current_.data(), total);
    }

private:
    const Events<T>* events_ = nullptr;
};

}
