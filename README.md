# Wind


## Build (library + tests)

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

SDL / OpenGL / NanoVG stay behind `ENGINE_WITH_WINDOW` (default **OFF** in this repo; local preset `vs-window`). Mixer stays behind `ENGINE_WITH_AUDIO` (default **OFF**; local preset `vs-audio`). `engine_tests` never calls `Engine::run` and never opens a mixer device.

Games consume this repo as a git submodule at `external/engine`:

```cmake
add_subdirectory(external/engine)
engine_add_game(my_game src/main.cpp)
```

`ENGINE_BUILD_TESTS` defaults OFF then. `ENGINE_WITH_WINDOW` defaults **ON** for that subdirectory so a remake does not have to FORCE it.
