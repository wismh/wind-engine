# Wind


## Build (library + tests)

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

SDL / mixer / OpenGL stay behind `ENGINE_WITH_WINDOW` / `ENGINE_WITH_AUDIO` (OFF until those slices).

Games consume this repo as a git submodule (`add_subdirectory(engine)`). `ENGINE_BUILD_TESTS` defaults OFF then.
