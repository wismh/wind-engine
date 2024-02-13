#include <engine/core/engine.h>
#include <engine/igame.h>

namespace {

class WindowSmokeGame final : public engine::GameBase {};

}

template bool engine::Engine<WindowSmokeGame>::init();
template int engine::Engine<WindowSmokeGame>::run();
template void engine::Engine<WindowSmokeGame>::dispose();
