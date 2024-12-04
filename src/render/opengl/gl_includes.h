#pragma once

#if defined(ENGINE_WITH_WINDOW)

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#elif __has_include(<glad/glad.h>)
#include <glad/glad.h>
#include <SDL3/SDL.h>
#elif __has_include(<glad/gl.h>)
#include <glad/gl.h>
#include <SDL3/SDL.h>
#else
#error "glad headers not found under external/glad (see external/README.md)"
#endif

#endif
