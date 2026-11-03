#include "core/relative_mouse.h"

#include <utility>

namespace engine {

RelativeMouse::RelativeMouse(Apply apply) : apply_(std::move(apply)) {}

bool RelativeMouse::request(WindowId window, bool on) {
    if (!on) {
        requested_.erase(window);
        if (active_.erase(window) > 0) {
            (void)apply_(window, false);
        }
        return false;
    }
    if (!apply_(window, true)) {
        requested_.erase(window);
        active_.erase(window);
        return false;
    }
    requested_.insert(window);
    active_.insert(window);
    return true;
}

bool RelativeMouse::requested(WindowId window) const noexcept {
    return requested_.contains(window);
}

bool RelativeMouse::active(WindowId window) const noexcept {
    return active_.contains(window);
}

void RelativeMouse::focus_lost(WindowId window) {
    if (active_.erase(window) > 0) {
        (void)apply_(window, false);
    }
}

void RelativeMouse::focus_gained(WindowId window) {
    if (!requested_.contains(window) || active_.contains(window)) {
        return;
    }
    if (apply_(window, true)) {
        active_.insert(window);
    }
}

void RelativeMouse::window_closed(WindowId window) {
    requested_.erase(window);
    active_.erase(window);
}

}
