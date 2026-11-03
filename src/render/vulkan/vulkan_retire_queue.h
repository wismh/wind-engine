#pragma once

// docs/tech/modules/Render.md

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace engine::render {

// GPU objects a frame in flight may still read, held until the submission that could use them has completed. Every
// submission goes to one queue, and a fence signalled by a submit covers all earlier submits on that queue, so one
// completed serial frees everything tagged at or below it. CPU only; `VulkanContext` holds the buffers.
template <class T>
class RetireQueue {
public:
    // Holds `item` until the next submission (the one being recorded now, if any) has completed.
    void retire(T item) {
        entries_.push_back(Entry{.serial = submitted_ + 1, .item = std::move(item)});
    }

    // The serial of a submission just made.
    [[nodiscard]] std::uint64_t submitted() {
        return ++submitted_;
    }

    // Submission `serial` has completed (its fence signalled). Returns the items no submission can read any more.
    // A serial at or below an earlier one frees nothing new.
    [[nodiscard]] std::vector<T> completed(std::uint64_t serial) {
        completed_ = std::max(completed_, serial);
        std::vector<T> done;
        std::erase_if(entries_, [&](Entry& entry) {
            if (entry.serial > completed_) {
                return false;
            }
            done.push_back(std::move(entry.item));
            return true;
        });
        return done;
    }

    // Every held item, for when the device is idle.
    [[nodiscard]] std::vector<T> take_all() {
        std::vector<T> done;
        done.reserve(entries_.size());
        for (Entry& entry : entries_) {
            done.push_back(std::move(entry.item));
        }
        entries_.clear();
        return done;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    struct Entry {
        std::uint64_t serial = 0;
        T item;
    };

    std::vector<Entry> entries_;
    std::uint64_t submitted_ = 0;
    std::uint64_t completed_ = 0;
};

} // namespace engine::render
