# Wind


## Build (library + tests)

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

SDL / OpenGL / NanoVG stay behind `ENGINE_WITH_WINDOW` (default **OFF**; local preset `vs-window`). Mixer stays behind `ENGINE_WITH_AUDIO` (default **OFF**; local preset `vs-audio`). `engine_tests` never calls `Engine::Run` and never opens a mixer device.

Games consume this repo as a git submodule (`add_subdirectory(engine)`). `ENGINE_BUILD_TESTS` defaults OFF then.
