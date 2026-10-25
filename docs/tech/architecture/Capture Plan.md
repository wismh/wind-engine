# Capture plan

This is a plan. It is not a description of the engine as it runs today. Nothing here exists yet: there is no `engine::capture`, no dynamic texture, and no render-to-texture.

## Goal

A game can open a physical camera (phone back/front, laptop webcam), receive frames, and draw them on screen with its own shaders. First target: a photo app with post-processing on Android, developed and tested on a Windows webcam. Backend: the SDL3 Camera API (`SDL_camera.h`), already in the tree.

## Naming

The engine already has an in-game camera: `ecs::Camera`, `CmdSetView`, `render_world_viewport` (`include/engine/ecs/camera.h`). The hardware camera must never share a word with it.

| Rule | Detail |
| --- | --- |
| Module / namespace / folder | `capture`: `engine::capture`, `include/engine/capture/`, `src/capture/`, `docs/tech/modules/Capture.md` |
| The word "camera" | Only `ecs::Camera` and the view. No `Camera*` type, file, or CMake option in the capture module. Docs say "capture device (a physical camera)" once, then "capture device" |
| Service | `ICapture` (frontend) / `CaptureSystem` (final class), same shape as `IHaptics` / `HapticsSystem`. Field `capture` on `EngineServices` |
| Types | `CaptureDeviceId`, `CaptureDeviceInfo`, `CaptureFormat`, `CapturePosition` (`Unknown`, `Front`, `Back`), `CapturePermission` (`Pending`, `Granted`, `Denied`), `CaptureSession`, `CaptureFrame`, `CaptureError` |
| Android | The manifest permission is `android.permission.CAMERA`; that is the OS's name and stays |

## Public API (target shape)

`include/engine/capture/capture_system.h`. No SDL in the header (Boundaries).

```cpp
struct CaptureFormat { int width; int height; float fps; };
struct CaptureDeviceInfo { CaptureDeviceId id; std::string name; CapturePosition position; std::vector<CaptureFormat> formats; };

// Valid until the next poll_frame() or close() of the session that gave it.
struct CaptureFrame { int width; int height; std::span<const std::uint8_t> rgba; double timestamp_seconds; };

class ICaptureSession {
public:
    virtual ~ICaptureSession() = default;
    virtual CapturePermission permission() const = 0;     // Pending until the user answers the OS prompt
    virtual std::optional<CaptureFormat> format() const = 0;  // nullopt while Pending
    virtual const CaptureFrame* poll_frame() = 0;          // nullptr: no new frame since the last call
};

class ICapture {
public:
    virtual bool init() = 0;
    virtual void dispose() = 0;
    virtual bool is_supported() const = 0;                 // runtime: any device present (API < 24 on Android: false)
    virtual std::vector<CaptureDeviceInfo> devices() = 0;
    virtual std::unique_ptr<ICaptureSession> open(CaptureDeviceId, std::optional<CaptureFormat> wanted) = 0;
};
```

Decisions:

- Pull model: the game calls `poll_frame()` in its frame system. No callbacks, no threads. Engine APIs stay main-thread only.
- Frames are always RGBA8, top row first (the same layout as `TextureDesc::rgba`). The backend converts whatever SDL hands out (`SDL_ConvertSurface` to `SDL_PIXELFORMAT_RGBA32`, YUV included) into one reusable buffer owned by the session. One copy per frame; a zero-copy path is out of scope (see Later).
- Permission is read with `SDL_GetCameraPermissionState`. No SDL events are needed, so `SdlGlPresentation::poll` is untouched.
- Like `IHaptics`, the backend is chosen inside `Impl`; there is no `ENGINE_WITH_CAPTURE` option. SDL's own `SDL_CAMERA` stays on. Drivers in the tree: Android (Camera2 NDK, dlopen'd, so `minSdk 21` still links; devices below API 24 report no devices), MediaFoundation (Windows), V4L2/PipeWire (Linux), CoreMedia (macOS), Emscripten (`getUserMedia`).
- `SDL_INIT_CAMERA` is initialized in `CaptureSystem::init` with `SDL_InitSubSystem`, the way `AudioSystem` does for audio.
- A `FakeCapture` (like `fake_haptics.h`) records the requests and serves a generated test pattern, so `engine_tests` need no device.

## Phases

Task numbers are assigned when the tasks are created. Each phase is one branch `feat/wind-N-...` from `dev`.

### 1. `ICapture` + SDL backend

- Headers and `src/capture/capture_system.cpp`, `fake_capture.h`, `EngineServices::capture`, wiring in `EngineHost`.
- **Editor Stop must close every open `CaptureSession`** (step 5 of Stop in [Editor Plan](Editor%20Plan.md)): nothing built from game code may survive `FreeLibrary`, and a webcam left open would stay lit.
- Tests `tests/capture_test.cpp`: fake backend, permission transitions, `poll_frame` returns nullptr without a new frame, RGBA layout and size, close/dispose order.
- Done when: a test game prints the device list and frame sizes on a Windows webcam. `Capture.md`, `Core.md`, `INDEX.md` updated.

### 2. Updatable texture

Today `ITexture` has `width()`/`height()` and `create_texture(TextureDesc)` uploads once; `OpenGLTexture` also flips rows on the CPU at creation. A frame per tick cannot go through that.

- `render::IDynamicTexture : ITexture` with `update(std::span<const uint8_t> rgba, int width, int height)` (`glTexSubImage2D`, reallocate on size change). Created by `IGraphicFactory::create_dynamic_texture(...)`.
- `update` flips the rows into a reused staging buffer, like creation does, so a dynamic texture behaves exactly like one made from the same data (v = 1 is the top row). One extra row-wise copy per frame (about 1 ms at 1080p) buys one convention for every `ITexture`.
- Bound where `ITexture` is accepted: an `IMaterial` sampler and a material override take the `shared_ptr<IDynamicTexture>` as is (the backend casts to `OpenGLTexture`). A UI `<Image>` resolves through `AssetId`; a runtime texture has no path in, so showing a frame in UI markup is left to phase 4.
- Tests: no GPU, so test the size-change and null-data contract on the factory interface with a test double; the GL side is checked by hand (Boundaries).

### 3. Android

- Game manifest overlay (`android/AndroidManifest.xml`) adds `<uses-permission android:name="android.permission.CAMERA"/>` and `<uses-feature android:name="android.hardware.camera" android:required="false"/>`. The engine template does not add it: a game that never opens a capture device must not ask for the permission.
- First `open` shows the OS prompt (SDL does this); the game shows `Pending` / `Denied` states in its UI.
- Lifecycle: on `DID_ENTER_BACKGROUND` SDL stops delivering frames. Decide whether the session survives resume or reports lost and the game reopens; cover with a test in the style of `android_lifecycle_test.cpp`.
- Orientation: SDL gives the sensor-oriented image. Rotation by display and sensor orientation is the game's concern in phase 1; revisit once a real device shows the cost.
- Done when: a sample draws the live feed on a physical phone, front and back.

### 4. Post-processing and saving a photo (separate plan)

Needs what the engine lacks today: render to a texture, readback of pixels, and a JPEG/PNG encoder (`stb_image` only reads). One-pass shader filters on the live feed need only phase 2. Write this plan after phase 3.

## Later

- Zero-copy `SurfaceTexture` / `samplerExternalOES` on Android for high resolutions.
- Manual exposure / focus / RAW: needs the Camera2 JNI path beside the SDL one.
- iOS: no toolchain or backend in the engine; SDL's CoreMedia driver would cover capture once one exists.

## Risks

- Android frame formats vary per phone (YUV variants, sensor rotation). Phase 3 is the real test; phase 1 on a webcam proves nothing about it.
- 1080p RGBA at 30 fps is about 250 MB/s of conversion and upload. Fine on a modern phone, but measure before promising 4K.
- `ITexture` consumers outside the renderer (UI) may assume a texture is immutable after creation.
