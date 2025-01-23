#include <gtest/gtest.h>

#include "ui/input_batch.h"

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Крок 4 (ui_scrollview_perf_plan.md): within one run_input() call, several MouseEvents can hit
// the same UiCanvas (e.g. a few Move events, or Wheel events during an active scroll) and nothing
// changes ViewModel data between them — no Game-phase system runs mid-run_input(). These tests
// exercise the *_for_run_input() entry points (src/ui/input_batch.h) directly, the same way
// run_input() (src/ecs/systems.cpp) does, and assert on UiInputBatchCache's own counters that the
// expensive bind+style+layout path (and update_drag()/update_pan()'s own item-owned
// apply_bindings() call) actually runs only once per canvas per batch — not once per event.
//
// They deliberately do NOT touch the public handle_pointer()/update_pointer_hover()/handle_wheel()/
// update_drag()/update_pan() used by every other UI test in this suite: those keep taking the
// batch==nullptr path (always a fresh, full recompute) unconditionally, proven by the fact that
// every pre-existing UI test in this repo (ui_scroll_test.cpp, ui_layout_hit_test.cpp,
// mvvm_test.cpp, ui_text_input_test.cpp, ...) still passes unmodified after this change.

namespace {

class ClickViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::RelayCommand click;

    ClickViewModel() {
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
    }
};

class VolumeChannelViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> fraction;

    VolumeChannelViewModel() { property(engine::ui::intern("fraction"), fraction); }
};

class LauncherViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<VolumeChannelViewModel>> channels;

    LauncherViewModel() { property(engine::ui::intern("channels"), channels); }
};

engine::ui::UiCanvas make_canvas(engine::render::Rect rect, int order = 0) {
    engine::ui::UiCanvas canvas;
    canvas.rect = rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.order = order;
    return canvas;
}

engine::ecs::Entity spawn_canvas(engine::ecs::World& world, std::shared_ptr<engine::ui::ViewModel> vm,
        std::string_view xml, engine::render::Rect rect, int order = 0) {
    const auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
    EXPECT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas(rect, order);
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});
    return entity;
}

}

TEST(UiInputBatch, RepeatedTouchesInSameBatchRecomputeOnce) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_canvas(world, vm,
            R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::UiInputBatchCache batch;

    // Simulates one run_input() call carrying a Down followed by several Move events, all inside
    // the same batch — the shape that motivated Крок 4.
    engine::ui::handle_pointer_for_run_input(world, 8.0f, 8.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_pointer_hover_for_run_input(world, 9.0f, 9.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_pointer_hover_for_run_input(world, 10.0f, 10.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_pointer_hover_for_run_input(world, 11.0f, 11.0f, engine::kPrimaryWindow, batch);

    EXPECT_EQ(vm->clicks, 1);
    EXPECT_EQ(batch.full_recompute_count, 1u);
}

TEST(UiInputBatch, SeparateBatchesEachRecomputeFreshNoStaleness) {
    // Guards against the exact trap a persistent ctx<>() batch counter would fall into (see
    // input_batch.h's header comment): a fresh UiInputBatchCache must always treat its first
    // touch as a cache miss, regardless of what any earlier, unrelated batch already did.
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_canvas(world, vm,
            R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);

    engine::ui::UiInputBatchCache first_batch;
    engine::ui::handle_pointer_for_run_input(world, 8.0f, 8.0f, engine::kPrimaryWindow, first_batch);
    ASSERT_EQ(first_batch.full_recompute_count, 1u);

    engine::ui::UiInputBatchCache second_batch;
    engine::ui::update_pointer_hover_for_run_input(world, 8.0f, 8.0f, engine::kPrimaryWindow, second_batch);
    EXPECT_EQ(second_batch.full_recompute_count, 1u);
}

TEST(UiInputBatch, DifferentCanvasesInSameBatchEachRecomputeOnce) {
    engine::ecs::World world;
    auto vm_a = std::make_shared<ClickViewModel>();
    auto vm_b = std::make_shared<ClickViewModel>();
    spawn_canvas(world, vm_a, R"(<Canvas><Button command="{binding click}" content="A"/></Canvas>)",
            {0.0f, 0.0f, 50.0f, 50.0f});
    spawn_canvas(world, vm_b, R"(<Canvas><Button command="{binding click}" content="B"/></Canvas>)",
            {100.0f, 0.0f, 50.0f, 50.0f});

    engine::ui::begin_frame(world);
    engine::ui::UiInputBatchCache batch;

    engine::ui::update_pointer_hover_for_run_input(world, 10.0f, 10.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_pointer_hover_for_run_input(world, 110.0f, 10.0f, engine::kPrimaryWindow, batch);
    EXPECT_EQ(batch.full_recompute_count, 2u);

    // A second touch of either canvas this same batch must still hit the cache.
    engine::ui::update_pointer_hover_for_run_input(world, 12.0f, 12.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_pointer_hover_for_run_input(world, 112.0f, 12.0f, engine::kPrimaryWindow, batch);
    EXPECT_EQ(batch.full_recompute_count, 2u);
}

constexpr std::string_view kVolumeChannelDragXml = R"(
<Canvas>
  <ItemsControl items_source="{binding channels}">
    <ItemTemplate>
      <Stack direction="vertical">
        <Image drag="{binding fraction}"/>
      </Stack>
    </ItemTemplate>
  </ItemsControl>
</Canvas>
)";

TEST(UiInputBatch, DragContinuationReusesSameBatchBindAndStillWritesCorrectly) {
    // update_drag_for_run_input() has its own, separate apply_bindings() call for an
    // ItemsControl-owned drag target (canvas.cpp update_drag_impl) — this proves that call is
    // skipped once prepare_top_canvas() already covered the same canvas entity earlier in the
    // batch (the Down that started the drag), while the write still lands on the right item
    // ViewModel (functional correctness unchanged from Mvvm.DragInsideItemsControlContinuesAcrossMoveEvents).
    engine::ecs::World world;
    auto vm = std::make_shared<LauncherViewModel>();
    auto master = std::make_shared<VolumeChannelViewModel>();
    auto music = std::make_shared<VolumeChannelViewModel>();
    vm->channels.set({master, music});
    spawn_canvas(world, vm, kVolumeChannelDragXml, {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::UiInputBatchCache batch;

    // ItemsControl stacks generated items vertically with the default 32x32 Image hug box each;
    // master's row is {0, 0, 32, 32}. x=8 is 8/32 = 0.25 on the default horizontal drag axis.
    engine::ui::handle_pointer_for_run_input(world, 8.0f, 8.0f, engine::kPrimaryWindow, batch);
    ASSERT_FLOAT_EQ(master->fraction.get(), 0.25f);
    ASSERT_EQ(batch.full_recompute_count, 1u);

    // A Move event's usual sequence: hover first (touches the same canvas entity — cache hit),
    // then the drag continuation.
    engine::ui::update_pointer_hover_for_run_input(world, 5000.0f, 5000.0f, engine::kPrimaryWindow, batch);
    engine::ui::update_drag_for_run_input(world, 5000.0f, 5000.0f, engine::kPrimaryWindow, batch);

    EXPECT_FLOAT_EQ(master->fraction.get(), 1.0f);
    EXPECT_FLOAT_EQ(music->fraction.get(), 0.0f);
    // Still just the one full recompute (the Down) for the whole batch...
    EXPECT_EQ(batch.full_recompute_count, 1u);
    // ...and update_drag_for_run_input's own apply_bindings() reused that instead of re-running it.
    EXPECT_EQ(batch.drag_or_pan_bindings_reused_count, 1u);
}

TEST(UiInputBatch, PublicApiIgnoresBatchCacheEntirely) {
    // The public, non-batched handle_pointer()/update_pointer_hover() (used by every other UI
    // test in this repo) must keep behaving exactly as before Крок 4: always a fresh recompute,
    // proven here by a ViewModel mutation between two direct calls taking effect immediately —
    // same shape as UiScroll.HitTestRespectsScrollOffsetAndClips, kept minimal here as a direct
    // regression guard colocated with the new batch-aware code path.
    engine::ecs::World world;
    auto vm = std::make_shared<LauncherViewModel>();
    auto first = std::make_shared<VolumeChannelViewModel>();
    auto second = std::make_shared<VolumeChannelViewModel>();
    vm->channels.set({first, second});
    spawn_canvas(world, vm, kVolumeChannelDragXml, {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    ASSERT_FLOAT_EQ(first->fraction.get(), 0.25f);

    // Reorder the list between two direct (non-batched) calls — second now occupies first's old
    // on-screen row. A stale cache would still write through the old (first) owner.
    vm->channels.set({second, first});
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);

    EXPECT_FLOAT_EQ(second->fraction.get(), 0.25f);
}
