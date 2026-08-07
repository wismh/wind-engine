#include "system_cursors.h"

namespace engine {
namespace {

SDL_SystemCursor system_cursor(ui::Cursor cursor) {
    switch (cursor) {
        case ui::Cursor::Pointer:
            return SDL_SYSTEM_CURSOR_POINTER;
        case ui::Cursor::Text:
            return SDL_SYSTEM_CURSOR_TEXT;
        case ui::Cursor::Crosshair:
            return SDL_SYSTEM_CURSOR_CROSSHAIR;
        case ui::Cursor::Wait:
            return SDL_SYSTEM_CURSOR_WAIT;
        case ui::Cursor::Progress:
            return SDL_SYSTEM_CURSOR_PROGRESS;
        case ui::Cursor::Move:
            return SDL_SYSTEM_CURSOR_MOVE;
        case ui::Cursor::NotAllowed:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        case ui::Cursor::EwResize:
            return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case ui::Cursor::NsResize:
            return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case ui::Cursor::NwseResize:
            return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case ui::Cursor::NeswResize:
            return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case ui::Cursor::Auto:
        case ui::Cursor::Default:
            break;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

}

SystemCursors::~SystemCursors() {
    destroy();
}

void SystemCursors::set(ui::Cursor cursor) {
    if (cursor == ui::Cursor::Auto) {
        cursor = ui::Cursor::Default;
    }
    if (shown_ == cursor) {
        return;
    }
    SDL_Cursor*& slot = cursors_[static_cast<std::size_t>(cursor)];
    if (slot == nullptr) {
        slot = SDL_CreateSystemCursor(system_cursor(cursor));
    }
    if (slot != nullptr && SDL_SetCursor(slot)) {
        shown_ = cursor;
    }
}

void SystemCursors::destroy() {
    for (SDL_Cursor*& cursor : cursors_) {
        if (cursor != nullptr) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    shown_.reset();
}

}
