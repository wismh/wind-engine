#pragma once

// docs/tech/build/CMake.md

// ENGINE_API marks a declaration that must cross the engine.dll boundary by name: functions that own
// mutable state a game module must share with the editor, and the build id. The editor build
// (ENGINE_EDITOR) makes engine shared and defines ENGINE_SHARED for every consumer; the engine's own
// translation units also get ENGINE_BUILDING. The exported game build is static and the macro is empty.
#if defined(ENGINE_SHARED)
#if defined(_WIN32)
#if defined(ENGINE_BUILDING)
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#endif
#else
#define ENGINE_API __attribute__((visibility("default")))
#endif
#else
#define ENGINE_API
#endif
