# Capture

A physical camera or webcam: list devices, open one, pull RGBA frames. One frontend, `ICapture`. The backend is chosen at compile time inside `CaptureSystem`. There is no `ENGINE_WITH_CAPTURE` option.

The word "camera" belongs to the in-game camera (`ecs::Camera`, `CmdSetView`). The hardware is always "capture" in the API and the docs.

## API

`include/engine/capture/capture_system.h`. `EngineServices::capture`.

| Call | Contract |
| --- | --- |
| `init` / `dispose` | `EngineHost::open_primary` calls `init()`. A failure only warns, like HTTP: every `open` then answers `CaptureError::Unsupported`. `dispose()` closes every session |
| `is_supported` | Whether any device is present right now. A runtime check |
| `devices` | `CaptureDeviceInfo`: `id`, `name`, `position` (`Front`, `Back`, `Unknown`), and the formats the device offers (size and frame rate; SDL's per-pixel-format duplicates are merged) |
| `open(id, wanted)` | `std::expected<std::unique_ptr<ICaptureSession>, CaptureError>`. `wanted` is a request: the offered format closest in pixel count, then in frame rate, is used. No `wanted`: the device's own choice. Errors: `Unsupported` (no backend, or no device at all), `NoSuchDevice`, `OpenFailed` |
| `close_all` | Closes every open session without destroying it. `EngineHost::detach_game` runs it when the editor stops a game, so no device outlives the game module |

`ICaptureSession` (main thread only; dropping it closes the device):

| Call | Contract |
| --- | --- |
| `permission` | `Pending` until the user answers the OS prompt, then `Granted` or `Denied`. A closed session is `Denied` |
| `format` | The format the device delivers. `nullopt` while not `Granted` |
| `poll_frame` | The newest frame since the last call, or `nullptr`. Never blocks. The `CaptureFrame` and its `rgba` span stay valid until the next `poll_frame` or `close` |
| `close` / `is_open` | Releases the device now. The session stays valid and answers `Denied` and `nullptr` |

`CaptureFrame::rgba` is RGBA8, sRGB, top row first: the layout of `render::TextureDesc::rgba`. The backend converts whatever the device delivers (RGB, packed or planar YUV) into one buffer owned by the session. That is one copy per frame.

There is no `update(dt)` and no callback: the game polls from a frame system.

## Backends

`src/capture/capture_system.cpp` branches on `ENGINE_WITH_WINDOW`:

| Build | Behavior |
| --- | --- |
| Window builds (SDL3) | `SDL_INIT_CAMERA` in `init`, `SDL_GetCameras`, `SDL_OpenCamera`, `SDL_AcquireCameraFrame`. SDL's drivers: Media Foundation (Windows), V4L2 and PipeWire (Linux), CoreMedia (macOS), Camera2 NDK (Android), `getUserMedia` (Web) |
| Otherwise | No devices. `is_supported()` is false. `open` answers `Unsupported` |

Permission comes from `SDL_GetCameraPermissionState`, so the session needs no SDL events.

### Android

SDL loads the Camera2 NDK with `dlopen`, so `minSdk 21` still links; a device below API 24 reports no capture device. The engine manifest template does not request the permission: a game that never opens a device must not ask for it. A game that does adds it in its manifest overlay (`android/AndroidManifest.xml`, see [Game Consumer](../build/Game%20Consumer.md#android-identity)):

```xml
<uses-permission android:name="android.permission.CAMERA"/>
<uses-feature android:name="android.hardware.camera" android:required="false"/>
```

The first `open` shows the OS prompt. The session is `Pending` until the user answers.

## Private pieces

| File | Role |
| --- | --- |
| `src/capture/format_select.h` | `pick_format`: the closest offered format. Pure, tested without a device |
| `src/capture/frame_convert.h` | `convert_surface_to_rgba`: any `SDL_Surface` to RGBA8 sRGB. Window builds |

## Tests

`tests/capture_test.cpp`: format choice, a system that was not initialized, init/dispose idempotence, an unknown device id, and surface conversion (packed RGB and YUY2). Nothing opens a real device.

## See also

- [Core](Core.md)
- [Capture plan](../architecture/Capture%20Plan.md)
