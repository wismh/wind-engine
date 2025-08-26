#pragma once

#include <engine/ui/cursor.h>

#include <SDL3/SDL.h>

#include <array>
#include <optional>

namespace engine {

// SDL system cursors, created on first use and kept until destroy(). SDL has one cursor for the whole process,
// shown over whichever window has the mouse, so this is not per window.
class SystemCursors {
public:
    SystemCursors() = default;
    SystemCursors(const SystemCursors&) = delete;
    SystemCursors& operator=(const SystemCursors&) = delete;
    ~SystemCursors();

    // Shows `cursor` when it differs from the last one set. Auto shows Default. A cursor the platform lacks
    // leaves the current one.
    void set(ui::Cursor cursor);
    // Frees every created cursor. Call before SDL_Quit.
    void destroy();

private:
    static constexpr std::size_t kCount = static_cast<std::size_t>(ui::Cursor::NeswResize) + 1;

    std::array<SDL_Cursor*, kCount> cursors_{};
    std::optional<ui::Cursor> shown_;
};

}
