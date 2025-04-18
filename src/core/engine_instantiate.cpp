#include <engine/core/engine.h>
#include <engine/core/engine_services.h>
#include <engine/igame.h>

namespace {

class WindowSmokeGame final : public engine::GameBase {
public:
    explicit WindowSmokeGame(const engine::EngineServices&) {}
};

}

template bool engine::Engine<WindowSmokeGame>::init();
template int engine::Engine<WindowSmokeGame>::run();
template void engine::Engine<WindowSmokeGame>::dispose();
