#pragma once

#if defined(ENGINE_WITH_WINDOW)

#if __has_include(<glad/glad.h>)
#include <glad/glad.h>
#elif __has_include(<glad/gl.h>)
#include <glad/gl.h>
#else
#error "glad headers not found under external/glad (see external/README.md)"
#endif

#include <SDL3/SDL.h>

#endif
