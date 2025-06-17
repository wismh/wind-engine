#pragma once

// docs/tech/modules/Core.md

#include <engine/render/command_buffer.h>
#include <engine/core/worlds.h>

namespace engine {

class AssetsDb;
class IAudioSystem;
class IHaptics;
class IHttpClient;
class InputSystem;
class IWindowControl;

namespace render {
class ICanvas;
class IGraphicFactory;
class IRenderBackend;
}

// References handed to a windowed game at construction. Engine::init owns every object;
// the game keeps only the ones it uses. Nothing here is reachable except what this struct passed in.
struct EngineServices {
    AssetsDb& assets;
    InputSystem& input;
    IAudioSystem& audio;
    IHaptics& haptics;
    IHttpClient& http;
    IWindowControl& windows;
    render::IGraphicFactory& graphics;
    render::IRenderBackend& backend;
    render::ICanvas& canvas;
    render::CommandBuffer& commands;
    Worlds& worlds;
};

}
