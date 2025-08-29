#include "scene_factory.h"

#include "clip_scene.h"
#include "hud_scene.h"
#include "inspector_scene.h"
#include "motion_scene.h"
#include "paint_mix_scene.h"
#include "table_scene.h"
#include "text_scene.h"

namespace bench {

std::unique_ptr<IScene> make_scene(const BenchCase& bench_case) {
    switch (bench_case.scene) {
        case BenchScene::Table:
            return std::make_unique<TableScene>(bench_case);
        case BenchScene::Inspector:
            return std::make_unique<InspectorScene>(bench_case);
        case BenchScene::Hud:
            return std::make_unique<HudScene>(bench_case);
        case BenchScene::PaintMix:
            return std::make_unique<PaintMixScene>(bench_case);
        case BenchScene::Motion:
            return std::make_unique<MotionScene>(bench_case);
        case BenchScene::Text:
            return std::make_unique<TextScene>(bench_case);
        case BenchScene::Clip:
            return std::make_unique<ClipScene>(bench_case);
    }
    return nullptr;
}

}
