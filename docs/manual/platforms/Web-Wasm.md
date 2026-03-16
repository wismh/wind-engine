# Web (WebAssembly)

Wind games compile to WebAssembly and WebGL2 using the Emscripten SDK (`emsdk`).

---

## 1. Prerequisites

1. Install and activate [emsdk](https://emscripten.org/docs/getting_started/downloads.html):
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Crucial Step:** Build a native `asset_codegen` executable first!
   > [!IMPORTANT]
   > The WebAssembly compiler cannot execute asset cooking tools during the build because the resulting binaries are compiled for WASM. You must compile `asset_codegen` natively beforehand.

---

## 2. Step 1: Build Native Host Codegen

In a clean host build directory:

```bash
cmake -S . -B build-host -DENGINE_BUILD_TESTS=OFF
cmake --build build-host --target asset_codegen
```

Note the location of the compiled tool:
- Linux / macOS: `$PWD/build-host/tools/asset_codegen/asset_codegen`
- Windows: `$PWD/build-host/tools/asset_codegen/Debug/asset_codegen.exe`

---

## 3. Step 2: Build Web Target

Run `emcmake` passing the path to the native tool:

```bash
emcmake cmake -B build-web -S . \
    -DENGINE_HOST_ASSET_CODEGEN="$PWD/build-host/tools/asset_codegen/asset_codegen" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build-web
```

---

## 4. Testing Locally

WebAssembly applications must be served over an HTTP server:

```bash
python3 -m http.server 8080 --directory build-web
```

Open `http://localhost:8080/my_game.html` in your browser.

---

## 5. Web-Specific Considerations

- **User Gesture for Audio:** Modern browsers mute WebAudio until the user interacts with the canvas. The engine buffers audio requests until the first user click or keypress.
- **IndexedDB Persistence:** File writes through `engine::user_data_directory` are mounted to IndexedDB and flushed asynchronously.

---

## Next Steps

- Package for mobile devices in [Android](Android.md).
- Follow essential C++ guidelines in [Rules & Conventions](../best-practices/Rules-and-Conventions.md).
