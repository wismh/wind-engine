#include "profile.h"
#include "profiler_chart.h"

#include <engine/ecs/events.h>
#include <engine/log.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/inspector.h>
#include <engine/ui/paint.h>
#include <engine/ui/presentation.h>
#include <engine/ui/profiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(ENGINE_UI_PROFILER)

namespace engine::ui {
    namespace {

        constexpr int kProfilerOrder = 10001;
        constexpr int kProfilerWindowWidth = 480;
        constexpr int kProfilerWindowHeight = 760;
        constexpr int kRingFrames = 120;
        static_assert(kChartSlots == kRingFrames);
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
            // Set by wind-cli `profile`. The clock runs when the window is open or this is set.
            bool capture = false;
            // A frame was pushed after capture turned on. Stays false across an empty commit.
            bool capture_saw_commit = false;
            std::optional<WindowId> panel_window;
            ecs::Entity selected{};
            std::map<ecs::Entity, OpenSlot> open;
            std::map<ecs::Entity, Ring<CanvasFrame>> rings;
            SharedFrame open_shared{};
            bool shared_touched = false;
            Ring<SharedFrame> shared;
        };

        ecs::World *g_world = nullptr;

        [[nodiscard]] bool recording() {
            if (g_world == nullptr) {
                return false;
            }
            const ProfilerState &state = g_world->ctx<ProfilerState>();
            return state.enabled || state.capture;
        }

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

        constexpr int kCanvasStageCount = 6;
        constexpr int kSharedStageCount = 2;

        constexpr glm::vec4 kCanvasStageColor[] = {
                {122.0f / 255.0f, 162.0f / 255.0f, 247.0f / 255.0f, 1.0f},
                {187.0f / 255.0f, 154.0f / 255.0f, 247.0f / 255.0f, 1.0f},
                {224.0f / 255.0f, 175.0f / 255.0f, 104.0f / 255.0f, 1.0f},
                {247.0f / 255.0f, 118.0f / 255.0f, 142.0f / 255.0f, 1.0f},
                {158.0f / 255.0f, 206.0f / 255.0f, 106.0f / 255.0f, 1.0f},
                {125.0f / 255.0f, 207.0f / 255.0f, 255.0f / 255.0f, 1.0f},
        };

        constexpr glm::vec4 kSharedStageColor[] = {
                {192.0f / 255.0f, 202.0f / 255.0f, 245.0f / 255.0f, 1.0f},
                {255.0f / 255.0f, 158.0f / 255.0f, 100.0f / 255.0f, 1.0f},
        };

        constexpr glm::vec4 kLayoutSkipColor{92.0f / 255.0f, 99.0f / 255.0f, 112.0f / 255.0f, 1.0f};
        constexpr glm::vec4 kBudgetColor{213.0f / 255.0f, 219.0f / 255.0f, 228.0f / 255.0f, 0.85f};

        void columns_from_canvas(const Ring<CanvasFrame> &ring, std::vector<ChartColumn> &out) {
            out.resize(static_cast<std::size_t>(ring.size));
            if (ring.size == 0) {
                return;
            }
            const int oldest = (ring.next - ring.size + kRingFrames) % kRingFrames;
            for (int i = 0; i < ring.size; ++i) {
                const CanvasFrame &frame = ring.frames[static_cast<std::size_t>((oldest + i) % kRingFrames)];
                ChartColumn &column = out[static_cast<std::size_t>(i)];
                column = {};
                column.stages_ns[0] = frame.bindings_ns;
                column.stages_ns[1] = frame.stylesheets_ns;
                column.stages_ns[2] = frame.input_ns;
                column.stages_ns[3] = frame.layout_ns;
                column.stages_ns[4] = frame.motion_ns;
                column.stages_ns[5] = frame.paint_ns;
                column.layout_skipped = frame.saw_paint && !frame.layout_ran;
            }
        }

        void columns_from_shared(const Ring<SharedFrame> &ring, std::vector<ChartColumn> &out) {
            out.resize(static_cast<std::size_t>(ring.size));
            if (ring.size == 0) {
                return;
            }
            const int oldest = (ring.next - ring.size + kRingFrames) % kRingFrames;
            for (int i = 0; i < ring.size; ++i) {
                const SharedFrame &frame = ring.frames[static_cast<std::size_t>((oldest + i) % kRingFrames)];
                ChartColumn &column = out[static_cast<std::size_t>(i)];
                column = {};
                column.stages_ns[0] = frame.begin_ns;
                column.stages_ns[1] = frame.commands_ns;
            }
        }

        void draw_chart(IDrawList &list, const render::Rect &content, const std::vector<ChartColumn> &columns,
                        int stage_count, bool layout_ticks, const glm::vec4 *colors) {
            const ChartGeometry geometry = build_chart(columns, stage_count, content.w, content.h, layout_ticks);
            for (const ChartRect &mark: geometry.rects) {
                if (mark.rect.w <= 0.0f || mark.rect.h <= 0.0f) {
                    continue;
                }
                const glm::vec4 color = mark.mark == ChartMark::LayoutSkip ? kLayoutSkipColor : colors[mark.stage];
                list.fill_rect(mark.rect, color);
            }
            if (geometry.budget.visible) {
                list.line(geometry.budget.from, geometry.budget.to, kBudgetColor, 1.0f);
            }
        }

        class ProfilerModel final : public ViewModel {
        public:
            Bindable<bool> pause{false};
            Bindable<std::string> stats;
            BindableList<std::shared_ptr<ProfilerRow>> rows;
            std::map<ecs::Entity, std::shared_ptr<ProfilerRow>> cache;
            ecs::World *world_ = nullptr;

            ProfilerModel() {
                property(intern("pause"), pause);
                property(intern("stats"), stats);
                property(intern("rows"), rows);
                stats.set("No frames yet");
                chart_ = [this](IDrawList &list, const render::Rect &content) { paint_canvas_chart(list, content); };
                shared_ = [this](IDrawList &list, const render::Rect &content) { paint_shared_chart(list, content); };
                paint(intern("chart"), chart_);
                paint(intern("shared"), shared_);
            }

            void paint_canvas_chart(IDrawList &list, const render::Rect &content) {
                if (world_ == nullptr) {
                    return;
                }
                const ProfilerState &state = world_->ctx<ProfilerState>();
                const auto found = state.rings.find(state.selected);
                if (found == state.rings.end() || found->second.size == 0) {
                    return;
                }
                columns_from_canvas(found->second, columns_);
                draw_chart(list, content, columns_, kCanvasStageCount, true, kCanvasStageColor);
            }

            void paint_shared_chart(IDrawList &list, const render::Rect &content) {
                if (world_ == nullptr) {
                    return;
                }
                const ProfilerState &state = world_->ctx<ProfilerState>();
                if (state.shared.size == 0) {
                    return;
                }
                columns_from_shared(state.shared, columns_);
                draw_chart(list, content, columns_, kSharedStageCount, false, kSharedStageColor);
            }

        private:
            RelayPaint chart_;
            RelayPaint shared_;
            std::vector<ChartColumn> columns_;
        };

        // Chrome inside #root, gap 6. Fixed siblings: title 22, two .section at 14, pause row 22,
        // chart 120, legend 16, shared 32, stats 200 = 440. Eight gaps = 48. The canvas list
        // subtracts 488 and keeps min-height 96px.
        constexpr std::string_view kProfilerCss = R"(
#profiler { background: #121418; }
#root { width: 100%; height: 100%; padding: 12px; gap: 6px; }
#title { height: 22px; font-size: 15px; color: #f3f5f8; font-family: default; }
.section { height: 14px; font-size: 11px; color: #8b93a3; font-family: default; }
#canvases {
    width: 100%;
    height: calc(100% - 488px);
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
#chart {
    width: 100%;
    height: 120px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
}
#shared {
    width: 100%;
    height: 32px;
    background: #1a1d24;
    border-width: 1px;
    border-color: #2e3440;
    border-radius: 8px;
}
.legend { width: 100%; height: 16px; gap: 8px; align-items: center; }
.key { height: 16px; gap: 4px; align-items: center; }
.swatch { width: 8px; height: 8px; border-radius: 2px; }
.sw-bind { background: #7aa2f7; }
.sw-style { background: #bb9af7; }
.sw-input { background: #e0af68; }
.sw-layout { background: #f7768e; }
.sw-motion { background: #9ece6a; }
.sw-paint { background: #7dcfff; }
.key-name {
    height: 16px;
    font-size: 11px;
    color: #8b93a3;
    font-family: default;
    align-items: center;
    white-space: nowrap;
}
.w-bind { width: 32px; }
.w-style { width: 36px; }
.w-input { width: 34px; }
.w-layout { width: 42px; }
.w-motion { width: 46px; }
.w-paint { width: 36px; }
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

            auto legend_key = [](std::string_view swatch, std::string_view name, std::string_view name_class) {
                auto item = stack().with_class("key").direction(StackDirection::Horizontal);
                item.add(component().with_class(swatch));
                item.add(label().with_class(name_class).text(name));
                return item;
            };
            auto legend = stack().with_id("legend").with_class("legend").direction(StackDirection::Horizontal);
            legend.add(legend_key("swatch sw-bind", "bind", "key-name w-bind"));
            legend.add(legend_key("swatch sw-style", "style", "key-name w-style"));
            legend.add(legend_key("swatch sw-input", "input", "key-name w-input"));
            legend.add(legend_key("swatch sw-layout", "layout", "key-name w-layout"));
            legend.add(legend_key("swatch sw-motion", "motion", "key-name w-motion"));
            legend.add(legend_key("swatch sw-paint", "paint", "key-name w-paint"));

            auto root_stack = stack().with_id("root").direction(StackDirection::Vertical);
            root_stack.add(label().with_id("title").text("UI Profiler"));
            root_stack.add(label().with_class("section").text("Canvases"));
            root_stack.add(std::move(canvases));
            root_stack.add(std::move(pause_row));
            root_stack.add(label().with_class("section").text("Frame"));
            root_stack.add(component().with_id("chart").paint_bind(intern("chart")));
            root_stack.add(std::move(legend));
            root_stack.add(component().with_id("shared").paint_bind(intern("shared")));
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
            presentation_of(world).sizes.sizes[kHeadlessProfilerWindow] =
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
            presentation_of(world).sizes.sizes.erase(id);
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
            model->world_ = &world;
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
            model->world_ = &world;
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

        std::string json_escape(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            for (const unsigned char c: text) {
                switch (c) {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if (c < 0x20) {
                            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                        } else {
                            out += static_cast<char>(c);
                        }
                        break;
                }
            }
            return out;
        }

        std::string stage_json(std::string_view name, const StageNumbers &numbers) {
            const auto ms = [](double ns) { return ns / 1000000.0; };
            return std::format("\"{}\":{{\"last_ms\":{:.4f},\"avg_ms\":{:.4f},\"max_ms\":{:.4f}}}", name,
                               ms(static_cast<double>(numbers.last)), ms(numbers.average),
                               ms(static_cast<double>(numbers.max)));
        }

        [[nodiscard]] bool any_ring(const ProfilerState &state) {
            for (const auto &[entity, ring]: state.rings) {
                (void) entity;
                if (ring.size > 0) {
                    return true;
                }
            }
            return false;
        }

        std::string snapshot_json(ecs::World &world, const ProfilerState &state) {
            std::vector<CanvasSource> sources;
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

            std::string out = state.pause ? "{\"paused\":true" : "{\"paused\":false";
            out += state.capture ? ",\"capturing\":true" : ",\"capturing\":false";
            out += ",\"canvases\":[";
            bool first = true;
            for (const CanvasSource &source: sources) {
                const UiInstance *instance = world.try_get<UiInstance>(source.entity);
                if (instance == nullptr) {
                    continue;
                }
                if (!first) {
                    out += ',';
                }
                first = false;
                const auto ring = state.rings.find(source.entity);
                const bool has = ring != state.rings.end() && ring->second.size > 0;
                const CanvasFrame *last = has ? ring->second.last() : nullptr;
                const bool layout_skipped = last != nullptr && last->saw_paint && !last->layout_ran;
                const auto stage = [&](std::string_view name, auto read) {
                    if (!has) {
                        return stage_json(name, StageNumbers{});
                    }
                    return stage_json(name, stage_numbers(ring->second, read));
                };
                out += std::format("{{\"window\":{},\"id\":\"{}\",\"frames\":{},\"elements\":{},\"generated\":{},"
                                   "\"layout_skipped\":{},\"stages\":{{",
                                   static_cast<std::uint32_t>(source.window), json_escape(instance->document.root.id),
                                   has ? ring->second.size : 0, last != nullptr ? last->elements : 0,
                                   last != nullptr ? last->generated : 0, layout_skipped ? "true" : "false");
                out += stage("bindings", [](const CanvasFrame &frame) { return frame.bindings_ns; });
                out += ',';
                out += stage("stylesheets", [](const CanvasFrame &frame) { return frame.stylesheets_ns; });
                out += ',';
                out += stage("input", [](const CanvasFrame &frame) { return frame.input_ns; });
                out += ',';
                out += stage("layout", [](const CanvasFrame &frame) { return frame.layout_ns; });
                out += ',';
                out += stage("motion", [](const CanvasFrame &frame) { return frame.motion_ns; });
                out += ',';
                out += stage("paint", [](const CanvasFrame &frame) { return frame.paint_ns; });
                out += "}}";
            }
            out += "],\"shared\":{";
            out += std::format("\"frames\":{}", state.shared.size);
            out += ',';
            out += stage_json("begin_frame",
                              stage_numbers(state.shared, [](const SharedFrame &frame) { return frame.begin_ns; }));
            out += ',';
            out += stage_json("commands",
                              stage_numbers(state.shared, [](const SharedFrame &frame) { return frame.commands_ns; }));
            out += "}}";
            return out;
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
        if (state.enabled || state.capture) {
            g_world = &world;
        } else if (g_world == &world) {
            g_world = nullptr;
        }
    }

    void profiler_commit_frame(ecs::World &world) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!state.enabled && !state.capture) {
            return;
        }
        if (state.pause) {
            reset_open(state);
            return;
        }
        bool pushed = false;
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
            pushed = true;
        }
        for (const ecs::Entity entity: dead) {
            state.open.erase(entity);
            state.rings.erase(entity);
        }
        if (state.shared_touched) {
            state.shared.push(state.open_shared);
            state.open_shared = {};
            state.shared_touched = false;
            pushed = true;
        }
        if (pushed && state.capture) {
            state.capture_saw_commit = true;
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
            destroy_panels(world);
            release_profiler_window(world);
            state.selected = {};
            state.pause = false;
            // CLI capture keeps the rings and the clock. Closing the window does not stop it.
            if (!state.capture) {
                if (g_world == &world) {
                    g_world = nullptr;
                }
                state.open.clear();
                state.rings.clear();
                state.shared = {};
                state.open_shared = {};
                state.shared_touched = false;
                state.capture_saw_commit = false;
            }
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

    void profiler_cli_set_capture(ecs::World &world, bool on) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!on) {
            state.capture = false;
            if (!state.enabled) {
                if (g_world == &world) {
                    g_world = nullptr;
                }
                state.open.clear();
                state.rings.clear();
                state.shared = {};
                state.open_shared = {};
                state.shared_touched = false;
                state.capture_saw_commit = false;
            }
            return;
        }
        if (!state.capture) {
            state.capture = true;
            state.capture_saw_commit = false;
        }
        g_world = &world;
    }

    bool profiler_cli_ready(ecs::World &world) {
        const ProfilerState &state = world.ctx<ProfilerState>();
        if (state.pause) {
            return true;
        }
        if (state.capture_saw_commit) {
            return true;
        }
        return state.enabled && (state.shared.size > 0 || any_ring(state));
    }

    std::string profiler_cli_json(ecs::World &world) { return snapshot_json(world, world.ctx<ProfilerState>()); }

} // namespace engine::ui

#endif
