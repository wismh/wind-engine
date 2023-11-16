#include <engine/core/engine.h>
#include <engine/igame.h>

namespace {

class WindowSmokeGame final : public engine::GameBase {};

}

template bool engine::Engine<WindowSmokeGame>::Init();
template int engine::Engine<WindowSmokeGame>::Run();
template void engine::Engine<WindowSmokeGame>::Dispose();
