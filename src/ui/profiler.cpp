#include "profile.h"

#include <engine/ui/canvas.h>
#include <engine/ui/profiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(ENGINE_UI_PROFILER)

namespace engine::ui {
    namespace {

        constexpr int kRingFrames = kProfilerRingFrames;
        static_assert(static_cast<std::size_t>(ProfileStage::Paint) + 1 == kProfilerStageCount);
        static_assert(static_cast<std::size_t>(ProfilerStage::Paint) + 1 == kProfilerStageCount);

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

            [[nodiscard]] std::vector<T> ordered() const {
                std::vector<T> out;
                out.reserve(static_cast<std::size_t>(size));
                const int oldest = (next - size + kRingFrames) % kRingFrames;
                for (int i = 0; i < size; ++i) {
                    out.push_back(frames[static_cast<std::size_t>((oldest + i) % kRingFrames)]);
                }
                return out;
            }
        };

        struct OpenSlot {
            ProfilerFrame frame{};
            bool touched = false;
        };

        struct ProfilerState {
            bool attached = false;
            bool pause = false;
            // Set by wind-cli `profile`. The clock runs when a panel is attached or this is set.
            bool capture = false;
            // A frame was pushed after capture turned on. Stays false across an empty commit.
            bool capture_saw_commit = false;
            ecs::Entity selected{};
            std::map<ecs::Entity, OpenSlot> open;
            std::map<ecs::Entity, Ring<ProfilerFrame>> rings;
            ProfilerSharedFrame open_shared{};
            bool shared_touched = false;
            Ring<ProfilerSharedFrame> shared;
        };

        // The world that records (attached or captured). One main-thread world at a time.
        ecs::World *g_profiled = nullptr;
        // The world of the current engine pass, or the profiled world while one of its canvases paints.
        // Scopes record only when this is the profiled world.
        ecs::World *g_current = nullptr;

        [[nodiscard]] bool recording_world(const ProfilerState &state) { return state.attached || state.capture; }

        [[nodiscard]] bool is_empty(const ecs::Entity &canvas) { return canvas.index == 0 && canvas.generation == 0; }

        void add_canvas_time(const ecs::Entity &canvas, ProfileStage stage, std::int64_t ns) {
            OpenSlot &slot = g_profiled->ctx<ProfilerState>().open[canvas];
            slot.touched = true;
            if (stage == ProfileStage::BeginFrame || stage == ProfileStage::CommandBuild) {
                return;
            }
            slot.frame.stage_ns[static_cast<std::size_t>(stage)] += ns;
            if (stage == ProfileStage::Bindings) {
                slot.frame.saw_bindings = true;
            }
        }

        void add_shared_time(ProfileStage stage, std::int64_t ns) {
            ProfilerState &state = g_profiled->ctx<ProfilerState>();
            state.shared_touched = true;
            if (stage == ProfileStage::BeginFrame) {
                state.open_shared.begin_frame_ns += ns;
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

        // Drops every sample and stops pointing the scopes at `world`. Pause and selection stay.
        void drop_samples(ecs::World &world, ProfilerState &state) {
            if (g_profiled == &world) {
                g_profiled = nullptr;
            }
            state.open.clear();
            state.rings.clear();
            state.shared = {};
            state.open_shared = {};
            state.shared_touched = false;
            state.capture_saw_commit = false;
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

        struct CanvasSource {
            ecs::Entity entity{};
            WindowId window = kPrimaryWindow;
            int order = 0;
            std::uint32_t index = 0;
        };

        std::vector<CanvasSource> live_canvases(ecs::World &world) {
            std::vector<CanvasSource> sources;
            auto view = world.view<UiCanvas>();
            for (ecs::Entity entity: view) {
                if (world.try_get<UiInstance>(entity) == nullptr) {
                    continue;
                }
                const UiCanvas &canvas = view.get<UiCanvas>(entity);
                sources.push_back(CanvasSource{entity, canvas.window, canvas.order, entity.index});
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
            return sources;
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

        constexpr std::array<std::string_view, kProfilerStageCount> kStageJsonNames{
                "bindings", "stylesheets", "input", "layout", "motion", "paint",
        };

        std::string snapshot_json(ecs::World &world, const ProfilerState &state) {
            std::string out = state.pause ? "{\"paused\":true" : "{\"paused\":false";
            out += state.capture ? ",\"capturing\":true" : ",\"capturing\":false";
            out += ",\"canvases\":[";
            bool first = true;
            for (const CanvasSource &source: live_canvases(world)) {
                const UiInstance &instance = world.get<UiInstance>(source.entity);
                if (!first) {
                    out += ',';
                }
                first = false;
                const auto ring = state.rings.find(source.entity);
                const bool has = ring != state.rings.end() && ring->second.size > 0;
                const ProfilerFrame *last = has ? ring->second.last() : nullptr;
                const bool layout_skipped = last != nullptr && last->layout_skipped();
                out += std::format("{{\"window\":{},\"id\":\"{}\",\"frames\":{},\"elements\":{},\"generated\":{},"
                                   "\"layout_skipped\":{},\"stages\":{{",
                                   static_cast<std::uint32_t>(source.window), json_escape(instance.document.root.id),
                                   has ? ring->second.size : 0, last != nullptr ? last->elements : 0,
                                   last != nullptr ? last->generated : 0, layout_skipped ? "true" : "false");
                for (std::size_t stage = 0; stage < kProfilerStageCount; ++stage) {
                    if (stage > 0) {
                        out += ',';
                    }
                    const StageNumbers numbers =
                            has ? stage_numbers(ring->second,
                                                [stage](const ProfilerFrame &frame) { return frame.stage_ns[stage]; })
                                : StageNumbers{};
                    out += stage_json(kStageJsonNames[stage], numbers);
                }
                out += "}}";
            }
            out += "],\"shared\":{";
            out += std::format("\"frames\":{}", state.shared.size);
            out += ',';
            out += stage_json("begin_frame", stage_numbers(state.shared, [](const ProfilerSharedFrame &frame) {
                                  return frame.begin_frame_ns;
                              }));
            out += ',';
            out += stage_json("commands", stage_numbers(state.shared, [](const ProfilerSharedFrame &frame) {
                                  return frame.commands_ns;
                              }));
            out += "}}";
            return out;
        }

    } // namespace

    UiProfileScope::UiProfileScope(const ecs::Entity &canvas, ProfileStage stage) : canvas_(&canvas), stage_(stage) {
        if (!profiler_recording()) {
            return;
        }
        active_ = true;
        start_ = std::chrono::steady_clock::now();
    }

    UiProfileScope::~UiProfileScope() {
        if (!active_ || canvas_ == nullptr || !profiler_recording() || is_empty(*canvas_)) {
            return;
        }
        const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_);
        add_canvas_time(*canvas_, stage_, elapsed.count());
    }

    UiProfileSharedScope::UiProfileSharedScope(ProfileStage stage) : stage_(stage) {
        if (!profiler_recording()) {
            return;
        }
        active_ = true;
        start_ = std::chrono::steady_clock::now();
    }

    UiProfileSharedScope::~UiProfileSharedScope() {
        if (!active_ || !profiler_recording()) {
            return;
        }
        const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_);
        add_shared_time(stage_, elapsed.count());
    }

    void profiler_attach(ecs::World &world) {
        g_current = &world;
        if (recording_world(world.ctx<ProfilerState>())) {
            g_profiled = &world;
        } else if (g_profiled == &world) {
            g_profiled = nullptr;
        }
    }

    bool profiler_recording() { return g_profiled != nullptr && g_current == g_profiled; }

    void profiler_begin_paint(const ecs::Entity &canvas) { g_current = is_empty(canvas) ? nullptr : g_profiled; }

    void profiler_commit_frame(ecs::World &world) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!recording_world(state)) {
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
            if (!world.valid(entity)) {
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
        if (!profiler_recording() || is_empty(canvas)) {
            return;
        }
        OpenSlot &slot = g_profiled->ctx<ProfilerState>().open[canvas];
        slot.touched = true;
        slot.frame.saw_paint = true;
        slot.frame.layout_ran = layout_ran;
        int elements = 0;
        int generated = 0;
        count_elements(root, elements, generated);
        slot.frame.elements = elements;
        slot.frame.generated = generated;
    }

    void set_ui_profiler_attached(ecs::World &world, bool attached) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (attached) {
            state.attached = true;
            g_profiled = &world;
            return;
        }
        state.attached = false;
        state.selected = {};
        state.pause = false;
        // CLI capture keeps the rings and the clock. A detached world never stays the scopes' target:
        // the editor destroys it right after.
        if (!state.capture) {
            drop_samples(world, state);
        } else if (g_profiled == &world) {
            g_profiled = nullptr;
        }
    }

    bool ui_profiler_attached(ecs::World &world) { return world.ctx<ProfilerState>().attached; }

    std::vector<ProfilerCanvas> profiler_canvases(ecs::World &world) {
        ProfilerState &state = world.ctx<ProfilerState>();
        const std::vector<CanvasSource> sources = live_canvases(world);
        const bool selected_live = std::any_of(sources.begin(), sources.end(), [&](const CanvasSource &source) {
            return source.entity == state.selected;
        });
        if (!selected_live) {
            state.selected = sources.empty() ? ecs::Entity{} : sources.front().entity;
        }
        std::unordered_set<WindowId> windows;
        for (const CanvasSource &source: sources) {
            windows.insert(source.window);
        }

        std::vector<ProfilerCanvas> canvases;
        canvases.reserve(sources.size());
        for (const CanvasSource &source: sources) {
            const UiInstance &instance = world.get<UiInstance>(source.entity);
            ProfilerCanvas canvas;
            canvas.canvas = source.entity;
            canvas.window = source.window;
            canvas.label = instance.document.root.id.empty() ? "Canvas" : instance.document.root.id;
            if (windows.size() > 1) {
                canvas.label.insert(0, std::format("[{}] ", static_cast<std::uint32_t>(source.window)));
            }
            canvas.selected = source.entity == state.selected;
            const auto ring = state.rings.find(source.entity);
            canvas.frames = ring != state.rings.end() ? ring->second.size : 0;
            canvases.push_back(std::move(canvas));
        }
        return canvases;
    }

    void profiler_select(ecs::World &world, ecs::Entity canvas) { world.ctx<ProfilerState>().selected = canvas; }

    ecs::Entity profiler_selected(ecs::World &world) { return world.ctx<ProfilerState>().selected; }

    void set_profiler_paused(ecs::World &world, bool paused) { world.ctx<ProfilerState>().pause = paused; }

    bool profiler_paused(ecs::World &world) { return world.ctx<ProfilerState>().pause; }

    std::vector<ProfilerFrame> profiler_frames(ecs::World &world, ecs::Entity canvas) {
        const ProfilerState &state = world.ctx<ProfilerState>();
        const auto ring = state.rings.find(canvas);
        if (ring == state.rings.end()) {
            return {};
        }
        return ring->second.ordered();
    }

    std::vector<ProfilerSharedFrame> profiler_shared_frames(ecs::World &world) {
        return world.ctx<ProfilerState>().shared.ordered();
    }

    void profiler_cli_set_capture(ecs::World &world, bool on) {
        ProfilerState &state = world.ctx<ProfilerState>();
        if (!on) {
            state.capture = false;
            if (!state.attached) {
                drop_samples(world, state);
            }
            return;
        }
        if (!state.capture) {
            state.capture = true;
            state.capture_saw_commit = false;
        }
        g_profiled = &world;
    }

    bool profiler_cli_ready(ecs::World &world) {
        const ProfilerState &state = world.ctx<ProfilerState>();
        if (state.pause) {
            return true;
        }
        if (state.capture_saw_commit) {
            return true;
        }
        return state.attached && (state.shared.size > 0 || any_ring(state));
    }

    std::string profiler_cli_json(ecs::World &world) { return snapshot_json(world, world.ctx<ProfilerState>()); }

} // namespace engine::ui

#endif
