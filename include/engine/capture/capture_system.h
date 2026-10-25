#pragma once

// docs/tech/modules/Capture.md

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine {

// A physical camera or webcam. The in-game camera is `ecs::Camera`; nothing in this header is that.
enum class CaptureDeviceId : std::uint32_t {};

enum class CapturePosition {
    Unknown,
    Front,
    Back,
};

enum class CapturePermission {
    // The OS prompt is up, or the device has not answered yet.
    Pending,
    Granted,
    // Denied by the user, or the session is closed.
    Denied,
};

enum class CaptureError {
    // No backend on this build, or no capture device on this machine.
    Unsupported,
    NoSuchDevice,
    OpenFailed,
};

struct CaptureFormat {
    int width = 0;
    int height = 0;
    float fps = 0.f;

    friend bool operator==(const CaptureFormat&, const CaptureFormat&) = default;
};

struct CaptureDeviceInfo {
    CaptureDeviceId id{};
    std::string name;
    CapturePosition position = CapturePosition::Unknown;
    std::vector<CaptureFormat> formats;
};

// RGBA8, top row first: the layout of `render::TextureDesc::rgba`. `rgba` points into the session's buffer and
// stays valid until the next `poll_frame` or `close` of the session that returned it.
struct CaptureFrame {
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> rgba;
    double timestamp_seconds = 0.0;
};

// One open capture device. Main thread only. Dropping it closes the device.
class ICaptureSession {
public:
    virtual ~ICaptureSession() = default;

    // `Pending` until the user answers the OS prompt. `Denied` once the session is closed.
    [[nodiscard]] virtual CapturePermission permission() const = 0;
    // The format the device delivers. `nullopt` while `Pending`, denied, or closed.
    [[nodiscard]] virtual std::optional<CaptureFormat> format() const = 0;
    // The newest frame the device has delivered since the last call, or `nullptr`. Never blocks.
    [[nodiscard]] virtual const CaptureFrame* poll_frame() = 0;
    // Releases the device now. The session stays valid and answers `Denied` and `nullptr` from then on.
    virtual void close() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
};

// Capture devices (cameras). One frontend; the backend is chosen at compile time inside `CaptureSystem`:
//   - Window builds: the SDL3 camera API (Media Foundation, V4L2 / PipeWire, CoreMedia, Android Camera2,
//     `getUserMedia`). On Android the game's manifest needs `android.permission.CAMERA`; the first `open`
//     shows the OS prompt.
//   - Others: no devices. `is_supported()` is false and `open` answers `CaptureError::Unsupported`.
//
// Main thread only. There is no `update`: a session is pulled with `poll_frame`.
class ICapture {
public:
    virtual ~ICapture() = default;

    virtual bool init() = 0;
    // Closes every open session.
    virtual void dispose() = 0;

    // Whether any capture device is present right now. A runtime check.
    [[nodiscard]] virtual bool is_supported() const = 0;
    [[nodiscard]] virtual std::vector<CaptureDeviceInfo> devices() = 0;
    // `wanted` is a request: the closest format the device offers is used (`ICaptureSession::format`).
    [[nodiscard]] virtual std::expected<std::unique_ptr<ICaptureSession>, CaptureError> open(
            CaptureDeviceId id, std::optional<CaptureFormat> wanted = std::nullopt) = 0;
    // Closes every open session without destroying it. The editor runs it when a game stops, so no device
    // outlives the game module.
    virtual void close_all() = 0;
};

class CaptureSystem final : public ICapture {
public:
    CaptureSystem();
    CaptureSystem(const CaptureSystem&) = delete;
    CaptureSystem& operator=(const CaptureSystem&) = delete;
    ~CaptureSystem() override;

    bool init() override;
    void dispose() override;
    [[nodiscard]] bool is_supported() const override;
    [[nodiscard]] std::vector<CaptureDeviceInfo> devices() override;
    [[nodiscard]] std::expected<std::unique_ptr<ICaptureSession>, CaptureError> open(
            CaptureDeviceId id, std::optional<CaptureFormat> wanted = std::nullopt) override;
    void close_all() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
