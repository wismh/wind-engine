#include <engine/capture/capture_system.h>

#include "format_select.h"
#include "frame_convert.h"

#include <engine/log.h>

#if defined(ENGINE_WITH_WINDOW)
#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#endif

#include <algorithm>
#include <utility>

namespace engine {
namespace {

// Sessions are owned by the game; the system only needs to reach them to close them. The session keeps the
// registry alive, so a session that outlives its system still unregisters safely.
struct Registry {
    std::vector<ICaptureSession*> sessions;

    void add(ICaptureSession* session) {
        sessions.push_back(session);
    }

    void remove(ICaptureSession* session) {
        std::erase(sessions, session);
    }

    void close_all() {
        // close() leaves the session registered, so a copy is not needed, but a copy keeps this safe if it ever does.
        const std::vector<ICaptureSession*> open = sessions;
        for (ICaptureSession* session : open) {
            session->close();
        }
    }
};

#if defined(ENGINE_WITH_WINDOW)

float fps_of(const SDL_CameraSpec& spec) {
    if (spec.framerate_denominator <= 0) {
        return 0.f;
    }
    return static_cast<float>(spec.framerate_numerator) / static_cast<float>(spec.framerate_denominator);
}

CaptureFormat format_of(const SDL_CameraSpec& spec) {
    return CaptureFormat{.width = spec.width, .height = spec.height, .fps = fps_of(spec)};
}

CapturePosition position_of(SDL_CameraPosition position) {
    switch (position) {
        case SDL_CAMERA_POSITION_FRONT_FACING:
            return CapturePosition::Front;
        case SDL_CAMERA_POSITION_BACK_FACING:
            return CapturePosition::Back;
        case SDL_CAMERA_POSITION_UNKNOWN:
        default:
            return CapturePosition::Unknown;
    }
}

// SDL lists one entry per pixel format; a game cares about size and rate only.
std::vector<CaptureFormat> formats_of(SDL_CameraID id) {
    std::vector<CaptureFormat> formats;
    int count = 0;
    SDL_CameraSpec** specs = SDL_GetCameraSupportedFormats(id, &count);
    if (specs == nullptr) {
        return formats;
    }
    for (int i = 0; i < count; ++i) {
        const CaptureFormat format = format_of(*specs[i]);
        if (std::ranges::find(formats, format) == formats.end()) {
            formats.push_back(format);
        }
    }
    SDL_free(specs);
    return formats;
}

class SdlSession final : public ICaptureSession {
public:
    SdlSession(SDL_Camera* camera, std::shared_ptr<Registry> registry)
        : camera_(camera), registry_(std::move(registry)) {
        registry_->add(this);
    }

    SdlSession(const SdlSession&) = delete;
    SdlSession& operator=(const SdlSession&) = delete;

    ~SdlSession() override {
        close();
        registry_->remove(this);
    }

    CapturePermission permission() const override {
        if (camera_ == nullptr) {
            return CapturePermission::Denied;
        }
        switch (SDL_GetCameraPermissionState(camera_)) {
            case SDL_CAMERA_PERMISSION_STATE_APPROVED:
                return CapturePermission::Granted;
            case SDL_CAMERA_PERMISSION_STATE_PENDING:
                return CapturePermission::Pending;
            case SDL_CAMERA_PERMISSION_STATE_DENIED:
            default:
                return CapturePermission::Denied;
        }
    }

    std::optional<CaptureFormat> format() const override {
        if (permission() != CapturePermission::Granted) {
            return std::nullopt;
        }
        SDL_CameraSpec spec{};
        if (!SDL_GetCameraFormat(camera_, &spec)) {
            return std::nullopt;
        }
        return format_of(spec);
    }

    const CaptureFrame* poll_frame() override {
        if (permission() != CapturePermission::Granted) {
            return nullptr;
        }
        Uint64 timestamp_ns = 0;
        SDL_Surface* surface = SDL_AcquireCameraFrame(camera_, &timestamp_ns);
        if (surface == nullptr) {
            return nullptr;
        }
        const int width = surface->w;
        const int height = surface->h;
        const bool converted = capture::convert_surface_to_rgba(surface, rgba_);
        SDL_ReleaseCameraFrame(camera_, surface);
        if (!converted) {
            return nullptr;
        }
        frame_ = CaptureFrame{
                .width = width,
                .height = height,
                .rgba = rgba_,
                .timestamp_seconds = static_cast<double>(timestamp_ns) / 1e9,
        };
        return &frame_;
    }

    void close() override {
        if (camera_ != nullptr) {
            SDL_CloseCamera(camera_);
            camera_ = nullptr;
        }
        frame_ = CaptureFrame{};
        rgba_.clear();
    }

    bool is_open() const override {
        return camera_ != nullptr;
    }

private:
    SDL_Camera* camera_ = nullptr;
    std::shared_ptr<Registry> registry_;
    std::vector<std::uint8_t> rgba_;
    CaptureFrame frame_;
};

#endif

}

struct CaptureSystem::Impl {
    std::shared_ptr<Registry> registry = std::make_shared<Registry>();
    bool initialized = false;
};

CaptureSystem::CaptureSystem() : impl_(std::make_unique<Impl>()) {}

CaptureSystem::~CaptureSystem() {
    dispose();
}

bool CaptureSystem::init() {
    if (impl_->initialized) {
        return true;
    }
#if defined(ENGINE_WITH_WINDOW)
    if (!SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        return false;
    }
#endif
    impl_->initialized = true;
    return true;
}

void CaptureSystem::dispose() {
    if (!impl_->initialized) {
        return;
    }
    impl_->registry->close_all();
#if defined(ENGINE_WITH_WINDOW)
    SDL_QuitSubSystem(SDL_INIT_CAMERA);
#endif
    impl_->initialized = false;
}

bool CaptureSystem::is_supported() const {
#if defined(ENGINE_WITH_WINDOW)
    if (!impl_->initialized) {
        return false;
    }
    int count = 0;
    SDL_CameraID* ids = SDL_GetCameras(&count);
    SDL_free(ids);
    return count > 0;
#else
    return false;
#endif
}

std::vector<CaptureDeviceInfo> CaptureSystem::devices() {
    std::vector<CaptureDeviceInfo> result;
#if defined(ENGINE_WITH_WINDOW)
    if (!impl_->initialized) {
        return result;
    }
    int count = 0;
    SDL_CameraID* ids = SDL_GetCameras(&count);
    if (ids == nullptr) {
        return result;
    }
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetCameraName(ids[i]);
        result.push_back(CaptureDeviceInfo{
                .id = static_cast<CaptureDeviceId>(ids[i]),
                .name = name != nullptr ? name : "",
                .position = position_of(SDL_GetCameraPosition(ids[i])),
                .formats = formats_of(ids[i]),
        });
    }
    SDL_free(ids);
#endif
    return result;
}

std::expected<std::unique_ptr<ICaptureSession>, CaptureError> CaptureSystem::open(
        CaptureDeviceId id, std::optional<CaptureFormat> wanted) {
#if defined(ENGINE_WITH_WINDOW)
    if (!impl_->initialized) {
        return std::unexpected(CaptureError::Unsupported);
    }
    const auto sdl_id = static_cast<SDL_CameraID>(id);
    int count = 0;
    SDL_CameraID* ids = SDL_GetCameras(&count);
    const bool known = ids != nullptr && std::find(ids, ids + count, sdl_id) != ids + count;
    SDL_free(ids);
    if (!known) {
        return std::unexpected(count == 0 ? CaptureError::Unsupported : CaptureError::NoSuchDevice);
    }

    // The format the game asked for, resolved to the device's own spec so SDL keeps the pixel format it offers.
    std::optional<SDL_CameraSpec> spec;
    if (wanted) {
        const std::vector<CaptureFormat> offered = formats_of(sdl_id);
        if (const std::optional<CaptureFormat> pick = capture::pick_format(offered, wanted)) {
            int spec_count = 0;
            SDL_CameraSpec** specs = SDL_GetCameraSupportedFormats(sdl_id, &spec_count);
            for (int i = 0; specs != nullptr && i < spec_count; ++i) {
                if (format_of(*specs[i]) == *pick) {
                    spec = *specs[i];
                    break;
                }
            }
            SDL_free(specs);
        }
    }

    SDL_Camera* camera = SDL_OpenCamera(sdl_id, spec ? &*spec : nullptr);
    if (camera == nullptr) {
        log::warn("Failed to open the capture device");
        return std::unexpected(CaptureError::OpenFailed);
    }
    return std::make_unique<SdlSession>(camera, impl_->registry);
#else
    (void)id;
    (void)wanted;
    return std::unexpected(CaptureError::Unsupported);
#endif
}

void CaptureSystem::close_all() {
    impl_->registry->close_all();
}

}
