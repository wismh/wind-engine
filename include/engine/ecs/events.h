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

// A persistent per-reader position into an Events<T> queue (SDD §9). Bevy gives every system
// parameter its own EventReader<T> with its own cursor, kept alive across frames by the
// framework; this engine's systems are plain std::function<void(World&)> closures with no such
// slot, so the cursor needs an explicit, caller-owned home instead — typically
// world.ctx<EventCursor<T>>() (see EventReader(World&, EventCursor<T>&) below), which gives one
// shared cursor per event type, correct for the common "exactly one per-frame reader of this
// type" case. A caller that genuinely needs a second, independent per-frame reader of the same T
// can hold its own separate EventCursor<T> instead of the world.ctx<> one.
template<typename T>
struct EventCursor {
    std::size_t next_id = 0;
};

template<typename T>
class Events {
    friend class EventReader<T>;

public:
    void send(T event) {
        current_.push_back(std::move(event));
        ++event_count_;
    }

    // previous_'s current contents are about to be permanently dropped (this is the *second*
    // update() since they were current_), so fold their count into oldest_id_ before swapping —
    // oldest_id_ always ends up equal to the id of previous_[0] (or, if previous_ is empty, of
    // current_[0]; if both are empty, the id the *next* send() will produce).
    void update() {
        oldest_id_ += previous_.size();
        previous_.clear();
        previous_.swap(current_);
    }

    // Total events ever sent through this queue — a cursor caught up to this value has seen
    // everything sent so far.
    [[nodiscard]] std::size_t event_count() const noexcept {
        return event_count_;
    }

private:
    std::vector<T> previous_;
    std::vector<T> current_;
    std::size_t event_count_ = 0;
    std::size_t oldest_id_ = 0;
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

    // Ad-hoc read: every event retained in the 2-generation buffer, every time — no memory across
    // constructions. Correct for one-off reads (tests, a system that intentionally wants "give me
    // whatever's still in the ~2-frame window"); a system that constructs this every frame will
    // see each event on both the frame it's sent and the following one — use the EventCursor<T>
    // overload below for that case instead.
    explicit EventReader(const Events<T>& events)
        : events_(&events), start_(0), end_(events.previous_.size() + events.current_.size()) {}
    explicit EventReader(World& world);

    // Persistent-cursor read: only events sent since `cursor.next_id`, and advances `cursor`
    // immediately (at construction, not lazily) to the queue's current event_count() — so this
    // exact cursor sees each event exactly once, however many more frames it lingers in the
    // buffer for some other/slower reader. If the cursor has fallen behind further than the
    // 2-generation buffer retains, the gap is silently clamped (those events are gone for good,
    // same as they would be for any reader — this is not expected to happen for the every-frame
    // readers this constructor is for).
    EventReader(const Events<T>& events, EventCursor<T>& cursor)
        : events_(&events)
        , start_(cursor.next_id > events.oldest_id_ ? cursor.next_id - events.oldest_id_ : std::size_t{0})
        , end_(events.previous_.size() + events.current_.size()) {
        if (start_ > end_) {
            start_ = end_;
        }
        cursor.next_id = events.event_count();
    }
    EventReader(World& world, EventCursor<T>& cursor);

    [[nodiscard]] Iterator begin() const {
        return Iterator(events_->previous_.data(), events_->previous_.size(), events_->current_.data(), start_);
    }

    [[nodiscard]] Iterator end() const {
        return Iterator(events_->previous_.data(), events_->previous_.size(), events_->current_.data(), end_);
    }

private:
    const Events<T>* events_ = nullptr;
    std::size_t start_ = 0;
    std::size_t end_ = 0;
};

}
