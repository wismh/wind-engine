#pragma once

// docs/tech/modules/Core.md (EngineHost::detach_game)

namespace engine {

class IWindowControl;

// The window state a stopped game must not leave behind, put back on the windows that stay open: no drag region and
// no click-through on kPrimaryWindow, overlay mode Auto, and on every open window no relative mouse mode and no
// mouse capture. EngineHost::detach_game calls it when the editor stops a game.
void reset_game_windows(IWindowControl& windows);

}
