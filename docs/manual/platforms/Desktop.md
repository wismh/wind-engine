# Desktop

Wind natively targets Windows, Linux, and macOS desktops with OpenGL 3.3 Core rendering and SDL3 windowing.

---

## 1. Building on Windows

Using Visual Studio 2022 / MSVC:

```bash
# Configure
cmake -B build -S .

# Build executable with debug symbols
cmake --build build --config RelWithDebInfo
```

The game executable and its associated assets folder (`assets/`) will be located in `build/RelWithDebInfo/`.

---

## 2. Building on Linux

Install required development packages (Ubuntu/Debian example):

```bash
sudo apt-get install build-essential cmake ninja-build \
    libasound2-dev libpulse-dev libgl1-mesa-dev
```

Build using Ninja:

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

---

## 3. Building on macOS

Requires Xcode Command Line Tools:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

---

## 4. Packaging for Distribution

When shipping your desktop binary, distribute the following:
1. `my_game` (or `my_game.exe`)
2. `assets/` (the cooked assets folder containing `catalog.toml` and processed data files)
3. Platform dynamic libraries (if dynamically linked with SDL or C++ runtime).

---

## Next Steps

- Target web browsers with [Web (WebAssembly)](Web-Wasm.md).
- Package for mobile devices in [Android](Android.md).
