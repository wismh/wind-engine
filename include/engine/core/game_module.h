#pragma once

// docs/tech/modules/Core.md

#include <engine/core/engine_services.h>
#include <engine/igame.h>

// The C ABI of a game module (`ENGINE_GAME` under `ENGINE_GAME_MODULE`). The editor resolves these
// three symbols by name. wind_game_build_id returns the kBuildId the game was compiled against, not
// engine::build_id(): inside the editor that call would reach the editor's own engine.

#if defined(_WIN32)
#define ENGINE_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define ENGINE_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace engine {

using CreateGameFn = IGame* (*)(const EngineServices& services);
using DestroyGameFn = void (*)(IGame* game);
using GameBuildIdFn = const char* (*)();

inline constexpr char kCreateGameSymbol[] = "wind_create_game";
inline constexpr char kDestroyGameSymbol[] = "wind_destroy_game";
inline constexpr char kGameBuildIdSymbol[] = "wind_game_build_id";

}
