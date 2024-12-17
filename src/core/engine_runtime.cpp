#include <engine/core/engine_runtime.h>

#include "render/opengl/opengl_runtime.h"
#include "render/opengl/window_control.h"
#include "render/opengl/window_manager.h"

#include <engine/audio/audio_system.h>
#include <engine/core/app_lifecycle.h>
#include <engine/core/fixed_step.h>
#include <engine/core/key_code.h>
#include <engine/core/platform.h>
#include <engine/core/time.h>
#include <engine/core/web_loop.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/font.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace engine {
namespace {

MouseButton mouse_button_from_sdl(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        default:
            return MouseButton::None;
    }
}

#if defined(__ANDROID__)
bool copy_sdl_io_file(const char* sdl_path, const std::filesystem::path& dest) {
    SDL_IOStream* io = SDL_IOFromFile(sdl_path, "rb");
    if (io == nullptr) {
        return false;
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        SDL_CloseIO(io);
        return false;
    }
    std::vector<char> buf(static_cast<std::size_t>(size));
    if (size > 0 && SDL_ReadIO(io, buf.data(), static_cast<std::size_t>(size)) != static_cast<std::size_t>(size)) {
        SDL_CloseIO(io);
        return false;
    }
    SDL_CloseIO(io);
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        return false;
    }
    if (!buf.empty()) {
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    }
    return static_cast<bool>(out);
}

// This SDL3 build has no Android-specific SDL_EnumerateDirectory/SDL_GetPathInfo backend
// (see external/SDL3/src/filesystem/android/SDL_sysfilesystem.c — only GetBasePath/GetPrefPath
// are implemented there), so directory enumeration over the packaged "assets://" tree never
// finds anything on Android; it silently walks zero entries. SDL_IOFromFile() by exact name
// does reach the APK's AAssetManager, though. So instead of enumerating, stage each asset the
// cooked catalog already lists by its known relative path.
void stage_catalog_assets(
        const std::filesystem::path& catalog_file, const std::string& sdl_prefix, const std::filesystem::path& dest_root) {
    std::ifstream in(catalog_file, std::ios::binary);
    if (!in) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = parse_cooked_catalog(text);
    if (!parsed) {
        return;
    }
    for (const CatalogEntry& entry : parsed->entries()) {
        copy_sdl_io_file((sdl_prefix + entry.relative_path).c_str(), dest_root / entry.relative_path);
    }
}

std::filesystem::path android_runtime_assets_root(const std::filesystem::path& base) {
    const char* storage = SDL_GetAndroidInternalStoragePath();
    const std::filesystem::path internal = storage != nullptr ? std::filesystem::path{storage} : std::filesystem::path{};
    const std::filesystem::path dest = default_assets_root(internal, Platform::Android);

    std::error_code ec;
    if (std::filesystem::exists(dest / "engine" / "catalog.toml", ec)) {
        return dest;
    }

    const std::string generic = base.generic_string();
    if (!base.empty() && generic.find("assets:") == std::string::npos && generic != "." && generic != "./") {
        const std::filesystem::path src = default_assets_root(base, Platform::Native);
        if (std::filesystem::is_directory(src, ec)) {
            stage_android_assets(src, dest);
            return dest;
        }
    }

    copy_sdl_io_file("catalog.toml", dest / "catalog.toml");
    copy_sdl_io_file("engine/catalog.toml", dest / "engine" / "catalog.toml");
    stage_catalog_assets(dest / "catalog.toml", "", dest);
    stage_catalog_assets(dest / "engine" / "catalog.toml", "engine/", dest / "engine");
    return dest;
}
#endif

}

struct EngineRuntime::Impl {
    std::shared_ptr<render::OpenGLFactory> factory = std::make_shared<render::OpenGLFactory>();
    std::shared_ptr<render::OpenGLRenderBackend> backend = std::make_shared<render::OpenGLRenderBackend>();
    // Owns the primary window plus any secondary ones opened later (SDD §21.5). The primary slot
    // exists from construction (see WindowManager's constructor) so window_control below — and
    // commands_ptr()/canvas_ptr() further down — have something valid to bind to even though no
    // real window exists yet at this point in Engine<GameT>::init()'s DI graph construction.
    WindowManager windows{*backend};
    std::shared_ptr<WindowControlImpl> window_control = std::make_shared<WindowControlImpl>(windows);
    // Cache of every font handed to load_ui_font/add_font, replayed into each secondary window's
    // own NanoVgPainter once its canvas is live (SDD §21.5/§21.6 — a secondary window has its own
    // independent painter/font atlas and nothing else ever loads a font into it). Mirrors the
    // WindowSizes backfill below: fonts_replayed_for tracks which windows already caught up so the
    // replay runs at most once per window, retried next frame if the canvas wasn't live yet.
    std::optional<Font> ui_font;
    // std::map, not unordered_map: AssetId has no std::hash specialization (only operator<=>), and
    // this cache is at most a handful of entries — ordering/lookup cost doesn't matter here.
    std::map<AssetId, Font> fonts;
    std::unordered_set<WindowId> fonts_replayed_for;
    bool video_inited = false;
    IGame* loop_game = nullptr;
    InputSystem* loop_input = nullptr;
    IAudioSystem* loop_audio = nullptr;
    std::unique_ptr<FixedStepClock> loop_clock;
    std::chrono::steady_clock::time_point loop_last{};
    std::function<void()> host_dispose;
    LoopShutdown loop_shutdown;
};

EngineRuntime::EngineRuntime() : impl_(std::make_unique<Impl>()) {}

EngineRuntime::~EngineRuntime() {
    shutdown();
}

bool EngineRuntime::init_video() {
    if (impl_->video_inited) {
        return true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    // Without this, a click on a background window that also focuses it is swallowed by SDL at
    // the OS level (its documented default is disabled) — no SDL_EVENT_MOUSE_BUTTON_DOWN fires for
    // that click, so the first click into any non-focused window (primary or secondary) appears to
    // do nothing until the user clicks again.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    impl_->video_inited = true;
    return true;
}

bool EngineRuntime::create_window(const WindowDesc& desc) {
    return impl_->windows.create_primary_window(desc);
}

std::optional<WindowId> EngineRuntime::open_window(const WindowDesc& desc) {
    return impl_->windows.create_window(desc);
}

void EngineRuntime::close_window(WindowId id) {
    impl_->windows.destroy_window(id);
}

void EngineRuntime::set_window_icon(const render::TextureDesc& desc) {
    impl_->windows.primary_window().set_icon(desc);
}

bool EngineRuntime::load_ui_font(const Font& font) {
    const auto canvas = impl_->windows.canvas_ptr(kPrimaryWindow);
    if (canvas == nullptr) {
        return false;
    }
    impl_->ui_font = font;
    return canvas->load_ui_font(font);
}

bool EngineRuntime::add_font(AssetId id, const Font& font) {
    const auto canvas = impl_->windows.canvas_ptr(kPrimaryWindow);
    if (canvas == nullptr) {
        return false;
    }
    impl_->fonts[id] = font;
    return canvas->add_font(id, font);
}

bool EngineRuntime::add_image(AssetId id, const render::TextureDesc& desc) {
    const auto canvas = impl_->windows.canvas_ptr(kPrimaryWindow);
    if (canvas == nullptr) {
        return false;
    }
    return canvas->add_image(id, desc);
}

void EngineRuntime::shutdown() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->windows.shutdown();
    if (impl_->video_inited) {
        SDL_Quit();
        impl_->video_inited = false;
    }
}

int EngineRuntime::run(IGame& game, InputSystem& input, IAudioSystem* audio, std::function<void()> host_dispose) {
    impl_->host_dispose = std::move(host_dispose);
    begin_loop(game, input, audio);

    const MainLoopPolicy policy{default_loop_kind()};
#if defined(__EMSCRIPTEN__)
    if (policy.uses_request_animation_frame()) {
        emscripten_set_main_loop_arg(&EngineRuntime::main_loop_thunk, this, 0, 1);
        return 0;
    }
#else
    (void)policy;
#endif

    ApplicationState& app = game.world().ctx<ApplicationState>();
    while (app.running) {
        tick_loop();
    }
    end_loop();
    return 0;
}

void EngineRuntime::begin_loop(IGame& game, InputSystem& input, IAudioSystem* audio) {
    impl_->loop_game = &game;
    impl_->loop_input = &input;
    impl_->loop_audio = audio;
    impl_->loop_clock = std::make_unique<FixedStepClock>(game.world().ctx<Time>(), game.world().ctx<ApplicationState>());
    impl_->loop_last = std::chrono::steady_clock::now();
    impl_->loop_shutdown = LoopShutdown{};

    // Mirrors Host's constructor (src/core/host.cpp): a secondary window's WindowSizes entry gets
    // backfilled every tick_loop() (see the loop below), but the primary window's ctx<WindowSize>()
    // had no equivalent — it stayed {0,0} until the first real SDL_EVENT_WINDOW_RESIZED, which a
    // fixed-size primary window that's never resized at startup never fires. That left
    // FillWindow/ScaleWithScreenSize canvases sized to {0,0} for on_start() and every frame before
    // any resize. write_window_size() reads the just-created primary window's real drawable size.
    write_window_size(game.world(), false);
    game.on_start();
    ui::apply_canvas_fit(game.world());
    game.world().ctx<ApplicationState>().running = true;

    // wind-89: from here until end_loop() clears it, WindowManager's Win32 modal-loop hook
    // (window_manager.cpp) calls reentrant_tick() on every WM_TIMER it sees — see that method's
    // doc comment for what it does and why it's safe to nest inside poll_events()'s SDL_PollEvent()
    // call specifically.
    impl_->windows.set_modal_loop_tick_callback(
            [this](std::optional<WindowId> dragged_window) { reentrant_tick(dragged_window); });
}

void EngineRuntime::tick_loop() {
    if (impl_->loop_game == nullptr || impl_->loop_input == nullptr || impl_->loop_clock == nullptr) {
        return;
    }
    IGame& game = *impl_->loop_game;
    ecs::World& world = game.world();
    Time& time = world.ctx<Time>();
    ApplicationState& app = world.ctx<ApplicationState>();

    const auto now = std::chrono::steady_clock::now();
    const float real_dt = std::chrono::duration<float>(now - impl_->loop_last).count();
    impl_->loop_last = now;

    world.flush_events();
    poll_events(world, *impl_->loop_input, app);

    // SDD §21.7 regression fix: click-through relies on MouseConsumed staying current every frame
    // (update_click_through() below reads it), and MouseConsumed only ever updates in reaction to
    // a real SDL_EVENT_MOUSE_MOTION (poll_events() above -> InputSystem::handle_mouse_move() ->
    // run_input()'s Move case -> ui::update_pointer_hover()). Once click-through is actually
    // applied on Windows (WS_EX_TRANSPARENT set), the OS stops delivering WM_MOUSEMOVE at all for
    // any point that now hit-tests as HTTRANSPARENT — so the moment the pointer sits over empty
    // (click-through) space, no further motion event ever arrives for this window again, even once
    // the pointer moves onto a real widget, and MouseConsumed gets stuck at whatever it last was:
    // both click-through and every button it "froze" over stop reacting to the mouse at all. Poll
    // the true OS cursor position directly every tick — the same technique other click-through
    // overlay apps use for this reason — instead of relying only on whichever motion events the OS
    // chose to deliver. Gated to when click-through could actually be engaged (kPrimaryWindow-only,
    // SDD §21.4) so every other window/game pays nothing extra here.
    if (WindowSystem& primary = impl_->windows.primary_window(); primary.click_through_enabled() && primary.is_transparent()) {
        if (const std::optional<glm::vec2> cursor = primary.cursor_client_position()) {
            impl_->loop_input->handle_mouse_move(kPrimaryWindow, *cursor, glm::vec2{0.0f, 0.0f});
        }
    }

    // Backfills a WindowSizes entry for any secondary window that has none yet (SDD §21.7) — a
    // freshly opened window has no drawable size in ui::WindowSizes until its first real
    // SDL_EVENT_WINDOW_RESIZED/PIXEL_SIZE_CHANGED event, which isn't guaranteed to fire
    // immediately after creation; without this, a FillWindow/ScaleWithScreenSize canvas targeting
    // it sizes itself to {0,0} for however many frames that takes. Only fills in *missing*
    // entries — never overwrites one a real resize event already kept current. Lives here (not in
    // WindowControlImpl/WindowManager) to keep the rendering/OS layer free of ecs::World& (§3.4/
    // §4.2) — this is the one place in EngineRuntime that already has both `impl_->windows` and
    // `world` in scope.
    {
        ui::WindowSizes& sizes = world.ctx<ui::WindowSizes>();
        bool backfilled = false;
        impl_->windows.for_each_secondary_window([&](WindowId id, WindowSystem& window) {
            if (!sizes.sizes.contains(id)) {
                const glm::ivec2 size = window.drawable_size();
                sizes.sizes[id] = ui::WindowSize{size.x, size.y};
                backfilled = true;
            }

            // Font backfill (same shape as the WindowSizes backfill above): a secondary window gets
            // its own independent OpenGLCanvas/NanoVgPainter with its own empty font atlas, and
            // nothing else ever loads a font into it — replay every font handed to
            // load_ui_font/add_font so far, once, as soon as this window's canvas is live. Retried
            // next frame (not marked done) if the canvas isn't live yet.
            if (!impl_->fonts_replayed_for.contains(id)) {
                if (render::OpenGLCanvas* canvas = impl_->windows.canvas(id)) {
                    // tick_loop() runs this backfill before draw_all(), so whatever GL context was
                    // left current by the *previous* frame's draw_all() (window order there is
                    // unordered) is not guaranteed to be this window's own context — NanoVG's
                    // load_ui_font/add_font need the right context bound.
                    SDL_GL_MakeCurrent(window.window(), canvas->native_context());
                    if (impl_->ui_font) {
                        (void)canvas->load_ui_font(*impl_->ui_font);
                    }
                    for (const auto& [font_id, font] : impl_->fonts) {
                        (void)canvas->add_font(font_id, font);
                    }
                    impl_->fonts_replayed_for.insert(id);
                }
            }
        });
        if (backfilled) {
            ui::apply_canvas_fit(world);
        }
    }

    ui::begin_frame(world);

    const int steps = impl_->loop_clock->advance(real_dt);
    if (impl_->loop_audio != nullptr) {
        impl_->loop_audio->update(time.delta_time);
    }
    for (int i = 0; i < steps; ++i) {
        game.on_fixed_update();
    }
    game.on_update();
    impl_->windows.primary_window().update_click_through(world.ctx<ui::MouseConsumed>().value);
    impl_->windows.draw_all();
}

void EngineRuntime::reentrant_tick(std::optional<WindowId> dragged_window) {
    // wind-89: called from WindowManager's Win32 modal-loop hook (window_manager.cpp), itself
    // firing on WM_TIMER (~10ms, USER_TIMER_MINIMUM) while the user is dragging or resizing a
    // window — the outer tick_loop() -> poll_events() -> SDL_PollEvent() call further up this
    // exact call stack (single thread, genuinely nested/reentrant, not concurrent) is paused
    // inside the OS's own modal move/size loop for as long as that continues. Deliberately not
    // identical to tick_loop():
    //
    //   - No world.flush_events() here. Events<T>::update() (ecs/events.h) ages previous_ into
    //     oblivion and promotes current_ into previous_; the outer tick_loop() already called it
    //     once for this real frame (the line right before poll_events()) before pausing here, so
    //     calling it *again* would clear out previous_ contents the outer frame's own systems —
    //     which haven't run yet; that happens once poll_events() eventually returns — are still
    //     depending on reading once execution resumes there. This reentrant tick doesn't need its
    //     own flush anyway: EventReader<T>::begin()/end() (events.h) index into previous_/current_
    //     fresh at every call rather than requiring a prior update(), so a system here that sends
    //     an event and a later system (same reentrant tick, or the next one) that reads it via a
    //     fresh EventReader/EventCursor construction sees it correctly with no flush involved —
    //     flushing is only about *aging across frame boundaries*, not same-tick delivery.
    //   - No poll_events(). Real OS input isn't flowing through SDL_PollEvent right now anyway —
    //     that's the entire reason this hook exists — and this callback receives Windows messages
    //     directly, so there's nothing for it to poll.
    //   - No secondary-window WindowSizes/font backfill (tick_loop()'s block right after
    //     poll_events()) — a cosmetic one-frame-late edge case if a new window happens to open in
    //     the exact same frame a drag starts, not worth the extra complexity here.
    //
    // wind-90 (td-over report): `dragged_window`'s own draw/swap was skipped below, not just here
    // in spirit — but only for an *opaque* window (wind-91 correction, see below). Dragging the
    // transparent/DWM-blur-behind primary overlay window was smooth — real time, no lag — but
    // dragging an *opaque* borderless window (a secondary window like td-over's
    // "workshop"/"settings") made the whole reentrant tick stall, including simulation that has
    // nothing to do with that window. The primary overlay's own on_update()/game logic never
    // stopped in either case — only OpenGLCanvas::draw()'s SDL_GL_SwapWindow call is suspected:
    // observed to block for the DWM compositor to catch up specifically for the one window
    // currently being live-moved/resized by the OS, worse for an opaque window than a transparent
    // one. Skipping that one window's draw/swap for the tick's duration keeps everything else —
    // game logic, every other live window — running at full speed; the skipped window's own
    // content simply doesn't redraw again until either the drag ends (tick_loop() resumes normal
    // drawing) or it stops being the active one (dragged_window changes). A window not being
    // freshly redrawn while the OS itself is actively moving it around the screen is an accepted,
    // common trade-off elsewhere (same idea as skipping the WindowSizes backfill above).
    //
    // wind-91 regression fix (td-over report): skipping unconditionally broke the one case that had
    // been perfect — dragging the *primary overlay itself*. It's transparent, so its own swap was
    // never the slow one (that's the whole reason wind-90 only ever suspected opaque windows); but
    // when the overlay is what's being dragged, it's also the only thing the player is looking at,
    // so skipping its redraw for the drag's duration reads as "the game stopped" even though
    // on_fixed_update()/on_update() never actually paused — a purely visual regression with zero
    // upside, since there was nothing to fix there in the first place. Below, the dragged window is
    // only ever excluded from draw_all() when it isn't transparent — matching the actual suspected
    // mechanism (opaque-window swap blocking) instead of "whichever window happens to be dragged".
    // td-over also reports workshop/settings dragging is still not fully smooth even with wind-90's
    // skip in place — i.e. skipping just the dragged window's own swap didn't fully explain the
    // stall either, hinting DWM may serialize composition more broadly than one window's Present
    // call during any live move/resize, not just the dragged window's own. Not yet re-investigated
    // with real measurements (td-over's own suggestion) — this fix only undoes the regression;
    // it does not claim to further improve the workshop/settings case.
    //
    // real_dt is measured against the exact same impl_->loop_last tick_loop() itself advances, and
    // updated every call here too — so no matter how many times this fires during one drag,
    // tick_loop()'s own real_dt once the drag ends and it resumes is just the small remainder since
    // the *last* reentrant_tick() call, never a multi-second "catch-up burst". FixedStepClock's own
    // accumulator (fixed_step.cpp) independently clamps any single call's real_dt to 0.25s and caps
    // steps at 8 (discarding, not deferring, any leftover past that) regardless of caller, so even
    // an unusually long gap between two calls can't run away either.
    if (impl_->loop_game == nullptr || impl_->loop_clock == nullptr) {
        return;
    }
    IGame& game = *impl_->loop_game;
    ecs::World& world = game.world();
    Time& time = world.ctx<Time>();

    const auto now = std::chrono::steady_clock::now();
    const float real_dt = std::chrono::duration<float>(now - impl_->loop_last).count();
    impl_->loop_last = now;

    ui::begin_frame(world);

    const int steps = impl_->loop_clock->advance(real_dt);
    if (impl_->loop_audio != nullptr) {
        impl_->loop_audio->update(time.delta_time);
    }
    for (int i = 0; i < steps; ++i) {
        game.on_fixed_update();
    }
    game.on_update();
    impl_->windows.primary_window().update_click_through(world.ctx<ui::MouseConsumed>().value);

    // wind-91: only skip the dragged window's own draw/swap when it's opaque — a transparent one's
    // swap was never the suspected bottleneck (see the wind-90/wind-91 doc comment above), so
    // skipping it bought nothing and cost a real, visible regression for the one case (dragging the
    // overlay itself) that had been perfect.
    std::optional<WindowId> skip_draw = dragged_window;
    if (skip_draw) {
        const WindowSystem* dragged = impl_->windows.window(*skip_draw);
        if (dragged == nullptr || dragged->is_transparent()) {
            skip_draw = std::nullopt;
        }
    }
    impl_->windows.draw_all(skip_draw);
}

void EngineRuntime::end_loop() {
    impl_->windows.set_modal_loop_tick_callback(nullptr);
    IGame* const game = impl_->loop_game;
    impl_->loop_game = nullptr;
    impl_->loop_input = nullptr;
    impl_->loop_audio = nullptr;
    impl_->loop_clock.reset();

    const std::function<void()> on_quit = game == nullptr ? std::function<void()>{}
                                                          : std::function<void()>{[game] { game->on_quit(); }};
    impl_->loop_shutdown.complete(on_quit, impl_->host_dispose);
}

void EngineRuntime::main_loop_thunk(void* self) {
    auto* runtime = static_cast<EngineRuntime*>(self);
    if (runtime == nullptr || runtime->impl_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    runtime->tick_loop();
    if (runtime->impl_->loop_game == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    if (!runtime->impl_->loop_game->world().ctx<ApplicationState>().running) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        runtime->end_loop();
    }
}

render::CommandBuffer& EngineRuntime::commands() {
    return *impl_->windows.commands_ptr(kPrimaryWindow);
}

render::ICanvas& EngineRuntime::canvas() {
    return *impl_->windows.canvas_ptr(kPrimaryWindow);
}

render::CommandBuffer* EngineRuntime::commands_for_window(WindowId id) {
    return impl_->windows.commands(id);
}

render::IGraphicFactory& EngineRuntime::factory() {
    return *impl_->factory;
}

render::IRenderBackend& EngineRuntime::backend() {
    return *impl_->backend;
}

std::shared_ptr<render::CommandBuffer> EngineRuntime::commands_ptr() const {
    return impl_->windows.commands_ptr(kPrimaryWindow);
}

std::shared_ptr<render::ICanvas> EngineRuntime::canvas_ptr() const {
    return impl_->windows.canvas_ptr(kPrimaryWindow);
}

std::shared_ptr<render::IGraphicFactory> EngineRuntime::factory_ptr() const {
    return impl_->factory;
}

std::shared_ptr<render::IRenderBackend> EngineRuntime::backend_ptr() const {
    return impl_->backend;
}

std::shared_ptr<IWindowControl> EngineRuntime::window_control_ptr() const {
    return impl_->window_control;
}

void* EngineRuntime::native_window() const {
    return impl_->windows.primary_window().window();
}

glm::ivec2 EngineRuntime::drawable_size() const {
    return impl_->windows.primary_window().drawable_size();
}

std::filesystem::path EngineRuntime::base_path() const {
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return {};
    }
    return std::filesystem::path(base);
}

std::filesystem::path EngineRuntime::assets_root() const {
#if defined(__ANDROID__)
    return android_runtime_assets_root(base_path());
#endif
    // Do not drop an empty SDL_GetBasePath(): on web that still maps to /assets
    // (SDD-WIND-WEB-001 §5). Native empty base stays empty via the helper.
    return default_assets_root(base_path());
}

void EngineRuntime::write_window_size(ecs::World& world, bool send_event) {
    const glm::ivec2 size = drawable_size();
    ui::WindowSize& ctx = world.ctx<ui::WindowSize>();
    ctx.width = size.x;
    ctx.height = size.y;
    if (send_event) {
        ecs::EventWriter<ui::WindowResizeEvent>{world}.send(
                ui::WindowResizeEvent{.window = kPrimaryWindow, .width = size.x, .height = size.y});
    }
    ui::apply_canvas_fit(world);
}

void EngineRuntime::poll_events(ecs::World& world, InputSystem& input, ApplicationState& app) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                app.quit();
                break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::WillEnterBackground);
                break;
            case SDL_EVENT_WILL_ENTER_FOREGROUND:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::DidEnterForeground);
                break;
            case SDL_EVENT_TERMINATING:
                apply_app_lifecycle(app, AppLifecycleEvent::Terminating);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                // write_window_size() always meant "the primary window's size" — resolving which
                // window actually resized (rather than assuming primary unconditionally) is the
                // §21.6 fix; defaulting to kPrimaryWindow on a failed lookup is defensive (e.g. a
                // stray event for a window that already closed).
                const WindowId resized = impl_->windows.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                if (resized == kPrimaryWindow) {
                    write_window_size(world, true);
                } else if (WindowSystem* secondary = impl_->windows.window(resized)) {
                    const glm::ivec2 size = secondary->drawable_size();
                    world.ctx<ui::WindowSizes>().sizes[resized] = ui::WindowSize{size.x, size.y};
                    ecs::EventWriter<ui::WindowResizeEvent>{world}.send(
                            ui::WindowResizeEvent{.window = resized, .width = size.x, .height = size.y});
                    ui::apply_canvas_fit(world);
                }
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                // Purely informational (SDD §21.7 — product decision): the engine never quits or
                // destroys anything here on its own. A game system reads WindowCloseRequestedEvent
                // in its own schedule and decides (quit, confirm dialog, ignore, close just this
                // window via IWindowControl::close_window).
                const WindowId closed = impl_->windows.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                ecs::EventWriter<ui::WindowCloseRequestedEvent>{world}.send(ui::WindowCloseRequestedEvent{.window = closed});
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat) {
                    const auto code = static_cast<KeyCode>(static_cast<std::uint32_t>(event.key.scancode));
                    input.handle_key(code, event.key.down);
                    if (event.key.down && code == KeyCode::AcBack) {
                        apply_android_back(app);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const WindowId window_id = impl_->windows.find_by_sdl_id(event.button.windowID).value_or(kPrimaryWindow);
                WindowSystem* window = impl_->windows.window(window_id);
                // wind-92 (SDD §21.7): a left-button-down inside the window's drag region starts a
                // manually-implemented drag (WindowSystem::begin_drag_if_in_region()) instead of
                // ever reaching the OS's native HTCAPTION/modal-loop path — consumed here exactly
                // like the old OS-native drag consumed it (the app never saw a button-down for an
                // HTCAPTION click either). The matching button-up ends it the same way.
                if (window != nullptr) {
                    if (event.button.down && event.button.button == SDL_BUTTON_LEFT &&
                            window->begin_drag_if_in_region(glm::vec2{event.button.x, event.button.y})) {
                        break;
                    }
                    if (!event.button.down && event.button.button == SDL_BUTTON_LEFT && window->is_dragging()) {
                        window->end_drag();
                        break;
                    }
                }
                input.handle_mouse_button(window_id, mouse_button_from_sdl(event.button.button), event.button.down,
                        glm::vec2{event.button.x, event.button.y});
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                const WindowId window_id = impl_->windows.find_by_sdl_id(event.motion.windowID).value_or(kPrimaryWindow);
                if (WindowSystem* window = impl_->windows.window(window_id); window != nullptr && window->is_dragging()) {
                    window->update_drag();
                    break;
                }
                input.handle_mouse_move(window_id, glm::vec2{event.motion.x, event.motion.y},
                        glm::vec2{event.motion.xrel, event.motion.yrel});
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                // wind-92 safety net: if a button-up ever gets missed (e.g. focus stolen mid-drag
                // by another app), don't leave the mouse captured and the window stuck "dragging"
                // forever.
                const WindowId window_id = impl_->windows.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                if (WindowSystem* window = impl_->windows.window(window_id); window != nullptr && window->is_dragging()) {
                    window->end_drag();
                }
                break;
            }
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                input.handle_touch(static_cast<std::uint32_t>(event.tfinger.fingerID),
                        event.type == SDL_EVENT_FINGER_DOWN, pos);
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                const glm::vec2 rel = denormalize_touch({event.tfinger.dx, event.tfinger.dy}, size);
                input.handle_touch_move(static_cast<std::uint32_t>(event.tfinger.fingerID), pos, rel);
                break;
            }
            default:
                break;
        }
    }
}

}
