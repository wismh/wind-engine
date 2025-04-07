#include "profile.h"

#include <engine/ecs/events.h>
#include <engine/log.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/inspector.h>
#include <engine/ui/profiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(ENGINE_UI_PROFILER)

namespace engine::ui {
    namespace {

        constexpr int kProfilerOrder = 10001;
        constexpr int kProfilerWindowWidth = 480;
        constexpr int kProfilerWindowHeight = 640;
        constexpr int kRingFrames = 120;
        // Headless tests have no ProfilerWindowHost. This id is never handed to the window manager.
        constexpr WindowId kHeadlessProfilerWindow{0xFFFFFFF1u};

        struct ProfilerCloseCursor {
            ecs::EventCursor<WindowCloseRequestedEvent> cursor;
        };

        struct CanvasFrame {
            std::int64_t bindings_ns = 0;
            std::int64_t stylesheets_ns = 0;
            std::int64_t input_ns = 0;
            std::int64_t layout_ns = 0;
            std::int64_t motion_ns = 0;
            std::int64_t paint_ns = 0;
            bool layout_ran = false;
            bool saw_paint = false;
            bool saw_bindings = false;
            int elements = 0;
            int generated = 0;
        };

        struct SharedFrame {
            std::int64_t begin_ns = 0;
            std::int64_t commands_ns = 0;
        };

        template<typename T>
        struct Ring {
            std::array<T, kRingFrames> frames{};
            int size = 0;
            int next = 0;

            void push(const T &frame) {
                frames[static_cast<std::size_t>(next)] = frame;
                next = (next + 1) % kRingFrames;
                if (size < kRingFrames) {
                    ++size;
                }
            }

            [[nodiscard]] const T *last() const {
                if (size == 0) {
                    return nullptr;
                }
                const int index = (next + kRingFrames - 1) % kRingFrames;
                return &frames[static_cast<std::size_t>(index)];
            }
        };

        struct OpenSlot {
            CanvasFrame frame{};
            bool touched = false;
        };

        struct ProfilerState {
            bool enabled = false;
            bool pause = false;
            std::optional<WindowId> panel_window;
            ecs::Entity selected{};
            std::map<ecs::Entity, OpenSlot> open;
            std::map<ecs::Entity, Ring<CanvasFrame>> rings;
            SharedFrame open_shared{};
            bool shared_touched = false;
            Ring<SharedFrame> shared;
        };

        ecs::World *g_world = nullptr;

        [[nodiscard]] bool recording() { return g_world != nullptr && g_world->ctx<ProfilerState>().enabled; }

        [[nodiscard]] bool canvas_is_tool(const ecs::Entity &canvas) {
            if (g_world == nullptr || (canvas.index == 0 && canvas.generation == 0)) {
                return false;
            }
            return g_world->try_get<InspectorPanel>(canvas) != nullptr ||
                   g_world->try_get<ProfilerPanel>(canvas) != nullptr;
        }

        void add_canvas_time(const ecs::Entity &canvas, ProfileStage stage, std::int64_t ns) {
            if (g_world == nullptr) {
                return;
            }
            OpenSlot &slot = g_world->ctx<ProfilerState>().open[canvas];
            slot.touched = true;
            CanvasFrame &frame = slot.frame;
            switch (stage) {
                case ProfileStage::Bindings:
                    frame.bindings_ns += ns;
                    frame.saw_bindings = true;
                    break;
                case ProfileStage::Stylesheets:
                    frame.stylesheets_ns += ns;
                    break;
                case ProfileStage::Input:
                    frame.input_ns += ns;
                    break;
                case ProfileStage::Layout:
                    frame.layout_ns += ns;
                    break;
                case ProfileStage::Motion:
                    frame.motion_ns += ns;
                    break;
                case ProfileStage::Paint:
                    frame.paint_ns += ns;
                    break;
                case ProfileStage::BeginFrame:
                case ProfileStage::CommandBuild:
                    break;
            }
        }

        void add_shared_time(ProfileStage stage, std::int64_t ns) {
            if (g_world == nullptr) {
                return;
            }
            ProfilerState &state = g_world->ctx<ProfilerState>();
            state.shared_touched = true;
            if (stage == ProfileStage::BeginFrame) {
                state.open_shared.begin_ns += ns;
            } else if (stage == ProfileStage::CommandBuild) {
                state.open_shared.commands_ns += ns;
            }
        }

        void count_elements(const Element &element, int &elements, int &generated) {
            ++elements;
            for (const Element &child: element.children) {
                count_elements(child, elements, generated);
            }
            for (const Element &child: element.generated_items) {
                ++generated;
                count_elements(child, elements, generated);
            }
        }

        void reset_open(ProfilerState &state) {
            for (auto &[entity, slot]: state.open) {
                (void) entity;
                slot.frame = {};
                slot.touched = false;
            }
            state.open_shared = {};
            state.shared_touched = false;
        }

        struct StageNumbers {
            std::int64_t last = 0;
            double average = 0.0;
            std::int64_t max = 0;
        };

        template<typename T, typename Read>
        StageNumbers stage_numbers(const Ring<T> &ring, Read read) {
            StageNumbers numbers;
            if (ring.size == 0) {
                return numbers;
            }
            std::int64_t sum = 0;
            const int oldest = (ring.next - ring.size + kRingFrames) % kRingFrames;
            for (int i = 0; i < ring.size; ++i) {
                const int index = (oldest + i) % kRingFrames;
                const std::int64_t value = read(ring.frames[static_cast<std::size_t>(index)]);
                sum += value;
                numbers.max = std::max(numbers.max, value);
            }
            numbers.last = read(*ring.last());
            numbers.average = static_cast<double>(sum) / static_cast<double>(ring.size);
            return numbers;
        }

        std::string stage_line(std::string_view name, const StageNumbers &numbers, std::string_view extra) {
            const auto ms = [](double ns) { return ns / 1000000.0; };
            return std::format("{}  last {:.2f} ms  avg {:.2f} ms  max {:.2f} ms{}", name,
                               ms(static_cast<double>(numbers.last)), ms(numbers.average),
                               ms(static_cast<double>(numbers.max)), extra);
        }

        class ProfilerRow final : public ViewModel {
        public:
            Bindable<std::string> label;
            Bindable<std::string> row_fill;
            RelayCommand select;
            ecs::World *world = nullptr;
            ecs::Entity canvas{};

            ProfilerRow() {
                property(intern("label"), label);
                property(intern("row"), row_fill);
                command(intern("select"), select);
                select = [this] { choose(); };
            }

            void choose() {
                if (world == nullptr) {
                    return;
                }
                world->ctx<ProfilerState>().selected = canvas;
            }
        };

        class ProfilerModel final : public ViewModel {
        public:
            Bindable<bool> pause{false};
            Bindable<std::string> stats;
            BindableList<std::shared_ptr<ProfilerRow>> rows;
            std::map<ecs::Entity, std::shared_ptr<ProfilerRow>> cache;

            ProfilerModel() {
                property(intern("pause"), pause);
                property(intern("stats"), stats);
                property(intern("rows"), rows);
                stats.set("No frames yet");
            }
        };

        // Chrome inside #root, which has gap 6 (five gaps = 30): title 22, two .section at 14,
        // pause row 22, stats 200. That is 272. The canvas list is the rest, at least 96px.
        constexpr std::string_view kProfilerCss = R"(
#profiler { background: #121418; }
#root { width: 100%; height: 100%; padding: 12px; gap: 6px; }
#title { height: 22px; font-size: 15px; color: #f3f5f8; font-family: default; }
.section { height: 14px; font-size: 11px; color: #8b93a3; font-family: default; }
#canvases {
    width: 100%;
    height: calc(100% - 302px);
    min-height: 96px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
    padding: 4px;
    overflow-y: auto;
    scrollbar-width: thin;
    scrollbar-color: #3a4150 #1a1d24;
}
.rows { width: 100%; }
.row {
    width: 100%;
    height: 24px;
    font-size: 13px;
    color: #e6ebf2;
    font-family: default;
    background: var(--row, #00000000);
    padding: 0 8px;
    margin: 0;
    white-space: nowrap;
    align-items: center;
    border-radius: 4px;
}
.row:hover { color: #ffffff; background: #ffffff14; }
.pause-row { width: 100%; height: 22px; gap: 8px; align-items: center; }
.pause-box {
    width: 16px;
    height: 16px;
    margin: 0;
    padding: 0;
    background: #121418;
    border-width: 1px;
    border-color: #3a4150;
    border-radius: 3px;
}
.pause-box:checked { background: #1c4634; border-color: #3dba6a; }
.pause-label { height: 22px; font-size: 13px; color: #e6ebf2; font-family: default; align-items: center; }
#stats {
    width: 100%;
    height: 200px;
    font-size: 12px;
    color: #d5dbe4;
    font-family: default;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
    padding: 8px;
    white-space: normal;
}
)";

        std::expected<UiDocument, UiError> build_profiler_document() {
            auto row = button().with_class("row")
                               .command_bind(intern("select"))
                               .content_bind(intern("label"))
                               .var("row", intern("row"));
            auto row_template = item_template();
            row_template.add(std::move(row));
            auto rows = items_control().with_class("rows").items_source_bind(intern("rows"));
            rows.add(std::move(row_template));
            auto canvases = scroll_view().with_id("canvases").overflow_y(Overflow::Scroll);
            canvases.add(std::move(rows));

            auto pause_box =
                    checkbox().with_id("pause").with_class("pause-box").checked(false).checked_bind(intern("pause"));
            auto pause_row = stack().with_class("pause-row").direction(StackDirection::Horizontal);
            pause_row.add(std::move(pause_box));
            pause_row.add(label().with_class("pause-label").text("Pause"));

            auto root_stack = stack().with_id("root").direction(StackDirection::Vertical);
            root_stack.add(label().with_id("title").text("UI Profiler"));
            root_stack.add(label().with_class("section").text("Canvases"));
            root_stack.add(std::move(canvases));
            root_stack.add(std::move(pause_row));
            root_stack.add(label().with_class("section").text("Frame"));
            root_stack.add(label().with_id("stats").text_bind(intern("stats")));

            auto root = canvas().with_id("profiler");
            root.add(std::move(root_stack));
            return make_document(std::move(root));
        }

        std::optional<Stylesheet> profiler_stylesheet() {
            std::vector<std::string> warnings;
            auto sheet = parse_css(kProfilerCss, warnings);
            for (const std::string &warning: warnings) {
                log::warn(std::format("ui profiler stylesheet: {}", warning));
            }
            if (!sheet) {
                log::error("ui profiler stylesheet failed to parse");
                return std::nullopt;
            }
            return std::move(*sheet);
        }

        render::Rect profiler_window_rect(ecs::World &world, WindowId window) {
            const WindowSize size = window_size_for(world, window);
            return render::Rect{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height)};
        }

        std::optional<WindowId> open_profiler_window(ecs::World &world) {
            WindowDesc desc;
            desc.title = "UI Profiler";
            desc.size = {kProfilerWindowWidth, kProfilerWindowHeight};
            ProfilerWindowHost &host = world.ctx<ProfilerWindowHost>();
            if (host.open) {
                return host.open(desc);
            }
            world.ctx<WindowSizes>().sizes[kHeadlessProfilerWindow] =
                    WindowSize{kProfilerWindowWidth, kProfilerWindowHeight};
            return kHeadlessProfilerWindow;
        }

        void release_profiler_window(ecs::World &world) {
            ProfilerState &state = world.ctx<ProfilerState>();
            if (!state.panel_window) {
                return;
            }
            const WindowId id = *state.panel_window;
            state.panel_window.reset();
            if (id == kPrimaryWindow) {
                return;
            }
            world.ctx<WindowSizes>().sizes.erase(id);
            if (id == kHeadlessProfilerWindow) {
                return;
            }
            ProfilerWindowHost &host = world.ctx<ProfilerWindowHost>();
            if (host.close) {
                host.close(id);
            }
        }

        std::optional<ecs::Entity> panel_entity(ecs::World &world) {
            std::optional<ecs::Entity> found;
            auto view = world.view<ProfilerPanel>();
            for (ecs::Entity entity: view) {
                found = entity;
            }
            return found;
        }

        void destroy_panels(ecs::World &world) {
            std::vector<ecs::Entity> entities;
            {
                auto view = world.view<ProfilerPanel>();
                for (ecs::Entity entity: view) {
                    entities.push_back(entity);
                }
            }
            for (ecs::Entity entity: entities) {
                world.destroy(entity);
            }
        }

        void ensure_panel(ecs::World &world, WindowId window) {
            const render::Rect rect = profiler_window_rect(world, window);
            if (const std::optional<ecs::Entity> existing = panel_entity(world)) {
                if (UiCanvas *canvas = world.try_get<UiCanvas>(*existing)) {
                    canvas->fit = UiFit::FillWindow;
                    canvas->rect = rect;
                    canvas->order = kProfilerOrder;
                    canvas->window = window;
                }
                return;
            }

            auto document = build_profiler_document();
            if (!document) {
                log::error("ui profiler document failed to build");
                return;
            }
            auto model = std::make_shared<ProfilerModel>();
            UiCanvas canvas;
            canvas.fit = UiFit::FillWindow;
            canvas.order = kProfilerOrder;
            canvas.window = window;
            canvas.rect = rect;
            canvas.data_context = model;
            const ecs::Entity entity =
                    spawn_canvas(world, std::move(canvas), std::move(*document), profiler_stylesheet());
            world.emplace<ProfilerPanel>(entity, ProfilerPanel{window});
        }

        struct CanvasSource {
            ecs::Entity entity{};
            WindowId window = kPrimaryWindow;
            int order = 0;
            std::uint32_t index = 0;
        };

        std::string canvas_stats(const ProfilerState &state, ecs::Entity selected) {
            const auto ring = state.rings.find(selected);
            if (ring == state.rings.end() || ring->second.size == 0) {
                return "No frames yet";
            }
            const CanvasFrame *last = ring->second.last();
            const auto line = [&](std::string_view name, auto read, std::string_view extra) {
                return stage_line(name, stage_numbers(ring->second, read), extra);
            };
            const std::string layout_extra = last != nullptr && last->saw_paint && !last->layout_ran ? "  skipped" : "";
            std::string text;
            text += line("bindings", [](const CanvasFrame &frame) { return frame.bindings_ns; }, "");
            text += '\n';
            text += line("stylesheets", [](const CanvasFrame &frame) { return frame.stylesheets_ns; }, "");
            text += '\n';
            text += line("input", [](const CanvasFrame &frame) { return frame.input_ns; }, "");
            text += '\n';
            text += line("layout", [](const CanvasFrame &frame) { return frame.layout_ns; }, layout_extra);
            text += '\n';
            text += line("motion", [](const CanvasFrame &frame) { return frame.motion_ns; }, "");
            text += '\n';
            text += line("paint", [](const CanvasFrame &frame) { return frame.paint_ns; }, "");
            text += '\n';
            text += std::format("elements {}\ngenerated {}", last != nullptr ? last->elements : 0,
                                last != nullptr ? last->generated : 0);
            text += "\n\n";
            const StageNumbers begin =
                    stage_numbers(state.shared, [](const SharedFrame &frame) { return frame.begin_ns; });
            const StageNumbers commands =
                    stage_numbers(state.shared, [](const SharedFrame &frame) { return frame.commands_ns; });
            text += stage_line("begin frame", begin, "");
            text += '\n';
            text += stage_line("commands", commands, "");
            return text;
        }

        void fill_panel(ecs::World &world, ecs::Entity panel) {
            UiCanvas *canvas = world.try_get<UiCanvas>(panel);
            UiInstance *instance = world.try_get<UiInstance>(panel);
            if (canvas == nullptr || instance == nullptr || !canvas->data_context) {
                return;
            }
            auto *model = dynamic_cast<ProfilerModel *>(canvas->data_context.get());
            if (model == nullptr) {
                return;
            }
            ProfilerState &state = world.ctx<ProfilerState>();
            state.pause = model->pause.get();

            std::vector<CanvasSource> sources;
            std::unordered_set<WindowId> source_windows;
            {
                auto view = world.view<UiCanvas>();
                for (ecs::Entity entity: view) {
                    if (world.try_get<ProfilerPanel>(entity) != nullptr ||
                        world.try_get<InspectorPanel>(entity) != nullptr) {
                        continue;
                    }
                    const UiCanvas &source = view.get<UiCanvas>(entity);
                    if (world.try_get<UiInstance>(entity) == nullptr) {
                        continue;
                    }
                    sources.push_back(CanvasSource{entity, source.window, source.order, entity.index});
                    source_windows.insert(source.window);
                }
            }
            std::stable_sort(sources.begin(), sources.end(), [](const CanvasSource &a, const CanvasSource &b) {
                if (a.window != b.window) {
                    return static_cast<std::uint32_t>(a.window) < static_cast<std::uint32_t>(b.window);
                }
                if (a.order != b.order) {
                    return a.order < b.order;
                }
                return a.index < b.index;
            });

            const bool selected_live = world.valid(state.selected) &&
                                       std::any_of(sources.begin(), sources.end(), [&](const CanvasSource &source) {
                                           return source.entity == state.selected;
                                       });
            if (!selected_live) {
                state.selected = sources.empty() ? ecs::Entity{} : sources.front().entity;
            }

            const bool show_window = source_windows.size() > 1;
            std::vector<std::shared_ptr<ProfilerRow>> visible;
            std::map<ecs::Entity, int> seen;
            for (const CanvasSource &source: sources) {
                UiInstance *source_instance = world.try_get<UiInstance>(source.entity);
                if (source_instance == nullptr) {
                    continue;
                }
                std::shared_ptr<ProfilerRow> &slot = model->cache[source.entity];
                if (!slot) {
                    slot = std::make_shared<ProfilerRow>();
                }
                ProfilerRow &row = *slot;
                row.world = &world;
                row.canvas = source.entity;
                std::string label =
                        source_instance->document.root.id.empty() ? "Canvas" : source_instance->document.root.id;
                if (show_window) {
                    label.insert(0, std::format("[{}] ", static_cast<std::uint32_t>(source.window)));
                }
                row.label.set(std::move(label));
                row.row_fill.set(source.entity == state.selected ? "#1c4634" : "#00000000");
                seen[source.entity] = 1;
                visible.push_back(slot);
            }
            for (auto it = model->cache.begin(); it != model->cache.end();) {
                if (!seen.contains(it->first)) {
                    it = model->cache.erase(it);
                } else {
                    ++it;
                }
            }
            model->rows.set(std::move(visible));
            if (!world.valid(state.selected)) {
                model->stats.set("No canvas");
                return;
            }
            model->stats.set(canvas_stats(state, state.selected));
        }

        bool consume_profiler_close(ecs::World &world) {
            const std::optional<WindowId> panel = world.ctx<ProfilerState>().panel_window;
            bool close = false;
            ecs::EventReader<WindowCloseRequestedEvent> reader(world, world.ctx<ProfilerCloseCursor>().cursor);
            for (const WindowCloseRequestedEvent &event: reader) {
                if (panel && event.window == *panel) {
                    close = true;
                }
            }
            if (!close) {
                return false;
            }
            set_ui_profiler_enabled(world, false);
            return true;
        }

    } // namespace

    UiProfileScope::UiProfileScope(const ecs::Entity &canvas, ProfileStage stage) : canvas_(&canvas), stage_(stage) {
        if (!recording() || canvas_is_tool(canvas)) {
            return;
        }
        active_ = true;
        start_ = std::chrono::steady_clock::now();
    }

    UiProfileScope::~UiProfileScope() {
        if (!active_ || canvas_ == nullptr || !recording() || canvas_is_tool(*canvas_)) {
            return;
        }
        if (canvas_->index == 0 && canvas_->generation == 0) {
            return;
        }
        const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_);
        add_canvas_time(*canvas_, stage_, elapsed.count());
    }

    UiProfileSharedScope::UiProfileSharedScope(ProfileStage stage) : stage_(stage) {
        if (!recording()) {
            return;
        }
        active_ = true;
        start_ = std::chrono::steady_clock::now();
    }

    UiProfileSharedScope::~UiProfileSharedScope() {
        if (!active_ || !recording()) {
            return;
        }
        const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_);
        add_shared_time(stage_, elapsed.count());
    }

    void profiler_attach(ecs::World &world) {
        // One main-thread world. A disabled world clears the pointer so a destroyed world's
        // address cannot stay armed into the next test.
        ProfilerState &state = world.ctx<ProfilerState>();
        g_world = state.enabled ? &world : nullptr;
    }

    void profiler_commit_frame(ecs::World &world) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!state.enabled) {
            return;
        }
        if (state.pause) {
            reset_open(state);
            return;
        }
        std::vector<ecs::Entity> dead;
        for (auto &[entity, slot]: state.open) {
            if (!slot.touched) {
                continue;
            }
            if (!world.valid(entity) || canvas_is_tool(entity)) {
                dead.push_back(entity);
                continue;
            }
            state.rings[entity].push(slot.frame);
            slot.frame = {};
            slot.touched = false;
        }
        for (const ecs::Entity entity: dead) {
            state.open.erase(entity);
            state.rings.erase(entity);
        }
        if (state.shared_touched) {
            state.shared.push(state.open_shared);
            state.open_shared = {};
            state.shared_touched = false;
        }
    }

    void profiler_finish_paint(const ecs::Entity &canvas, const Element &root, bool layout_ran) {
        if (!recording() || (canvas.index == 0 && canvas.generation == 0) || canvas_is_tool(canvas)) {
            return;
        }
        OpenSlot &slot = g_world->ctx<ProfilerState>().open[canvas];
        slot.touched = true;
        slot.frame.saw_paint = true;
        slot.frame.layout_ran = layout_ran;
        int elements = 0;
        int generated = 0;
        count_elements(root, elements, generated);
        slot.frame.elements = elements;
        slot.frame.generated = generated;
    }

    ProfileSample profiler_canvas_sample(ecs::World &world, ecs::Entity canvas) {
        const ProfilerState &state = world.ctx<ProfilerState>();
        const auto ring = state.rings.find(canvas);
        ProfileSample sample;
        if (ring == state.rings.end() || ring->second.size == 0) {
            return sample;
        }
        const CanvasFrame *last = ring->second.last();
        sample.stored = true;
        sample.frames = ring->second.size;
        if (last != nullptr) {
            sample.layout_ran = last->layout_ran;
            sample.saw_paint = last->saw_paint;
            sample.saw_bindings = last->saw_bindings;
            sample.elements = last->elements;
            sample.generated = last->generated;
        }
        return sample;
    }

    ProfileSample profiler_shared_sample(ecs::World &world) {
        const ProfilerState &state = world.ctx<ProfilerState>();
        ProfileSample sample;
        sample.stored = state.shared.size > 0;
        sample.frames = state.shared.size;
        return sample;
    }

    ecs::Entity profiler_selected(ecs::World &world) { return world.ctx<ProfilerState>().selected; }

    void set_ui_profiler_enabled(ecs::World &world, bool enabled) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!enabled) {
            state.enabled = false;
            if (g_world == &world) {
                g_world = nullptr;
            }
            destroy_panels(world);
            release_profiler_window(world);
            state.selected = {};
            state.pause = false;
            state.open.clear();
            state.rings.clear();
            state.shared = {};
            state.open_shared = {};
            state.shared_touched = false;
            return;
        }
        state.enabled = true;
        g_world = &world;
        sync_profiler_frames(world);
        sync_profiler_content(world);
    }

    bool ui_profiler_enabled(ecs::World &world) { return world.ctx<ProfilerState>().enabled; }

    void sync_profiler_frames(ecs::World &world) {
        if (!ui_profiler_enabled(world)) {
            return;
        }
        if (consume_profiler_close(world)) {
            return;
        }
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!state.panel_window) {
            state.panel_window = open_profiler_window(world);
        }
        if (!state.panel_window) {
            return;
        }
        ensure_panel(world, *state.panel_window);
    }

    void sync_profiler_content(ecs::World &world) {
        if (!ui_profiler_enabled(world)) {
            return;
        }
        const std::optional<ecs::Entity> panel = panel_entity(world);
        if (!panel) {
            return;
        }
        fill_panel(world, *panel);
    }

} // namespace engine::ui

#endif
