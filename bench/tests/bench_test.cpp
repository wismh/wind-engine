#include <gtest/gtest.h>

#include "bench_matrix.h"
#include "bench_options.h"
#include "bench_random.h"
#include "bench_report.h"
#include "bench_schedule.h"
#include "frame_plan.h"
#include "hud_data.h"
#include "scene_points.h"
#include "table_data.h"
#include "text_data.h"

#include <engine/ui/document.h>
#include <engine/ui/profiler.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::expected<bench::BenchOptions, std::string> parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list(args);
    return bench::parse_bench_options(list);
}

std::expected<bench::BenchCase, std::string> resolve(std::initializer_list<std::string_view> args) {
    const auto options = parse(args);
    if (!options) {
        return std::unexpected(options.error());
    }
    return bench::resolve_bench_case(*options);
}

}

TEST(BenchOptions, DefaultsMeasureExactlyTheRing) {
    const auto options = parse({"--scene", "table", "--mode", "quiet"});
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->scene, "table");
    EXPECT_EQ(options->mode, "quiet");
    EXPECT_TRUE(options->variant.empty());
    EXPECT_EQ(options->warmup, 60);
    EXPECT_EQ(options->frames, engine::ui::kProfilerRingFrames);
    EXPECT_EQ(options->width, 1600);
    EXPECT_EQ(options->height, 900);
    EXPECT_TRUE(options->out.empty());
    EXPECT_FALSE(options->list);
}

TEST(BenchOptions, ReadsEveryFlag) {
    const auto options = parse({"--scene", "table", "--mode", "hover", "--variant", "layout", "--warmup", "7",
            "--frames", "10", "--out", "runs/table.json", "--width", "1280", "--height", "720"});
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->variant, "layout");
    EXPECT_EQ(options->warmup, 7);
    EXPECT_EQ(options->frames, 10);
    EXPECT_EQ(options->out, std::filesystem::path("runs/table.json"));
    EXPECT_EQ(options->width, 1280);
    EXPECT_EQ(options->height, 720);
}

TEST(BenchOptions, ListAndHelpNeedNoScene) {
    const auto list = parse({"--list"});
    ASSERT_TRUE(list.has_value());
    EXPECT_TRUE(list->list);
    const auto help = parse({"--help"});
    ASSERT_TRUE(help.has_value());
    EXPECT_TRUE(help->help);
}

TEST(BenchOptions, RefusesBadArguments) {
    EXPECT_FALSE(parse({}).has_value()) << "no scene or mode";
    EXPECT_FALSE(parse({"--scene", "table"}).has_value()) << "no mode";
    EXPECT_FALSE(parse({"--scene", "table", "--mode"}).has_value()) << "a flag without its value";
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--speed", "2"}).has_value()) << "unknown flag";
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "stray"}).has_value()) << "stray argument";
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--frames", "0"}).has_value());
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--frames", "121"}).has_value()) << "past the ring";
    EXPECT_TRUE(parse({"--scene", "table", "--mode", "quiet", "--frames", "120"}).has_value());
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--warmup", "4"}).has_value()) << "before setup";
    EXPECT_TRUE(parse({"--scene", "table", "--mode", "quiet", "--warmup", "5"}).has_value());
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--frames", "1x"}).has_value());
    EXPECT_FALSE(parse({"--scene", "table", "--mode", "quiet", "--width", "100"}).has_value());

    const auto error = parse({"--scene", "table", "--mode", "quiet", "--frames", "500"});
    ASSERT_FALSE(error.has_value());
    EXPECT_NE(error.error().find("--frames"), std::string::npos) << error.error();
}

TEST(BenchOptions, ResolvesACaseOrSaysWhatIsValid) {
    const auto hover = resolve({"--scene", "inspector", "--mode", "hover", "--variant", "paint"});
    ASSERT_TRUE(hover.has_value()) << hover.error();
    EXPECT_EQ(hover->scene, bench::BenchScene::Inspector);
    EXPECT_EQ(hover->mode, bench::BenchMode::Hover);
    EXPECT_EQ(hover->variant, "paint");

    const auto scene = resolve({"--scene", "chart", "--mode", "quiet"});
    ASSERT_FALSE(scene.has_value());
    EXPECT_NE(scene.error().find("paint-mix"), std::string::npos) << scene.error();

    const auto mode = resolve({"--scene", "hud", "--mode", "scroll"});
    ASSERT_FALSE(mode.has_value());
    EXPECT_NE(mode.error().find("one-change"), std::string::npos) << mode.error();

    const auto needs_variant = resolve({"--scene", "table", "--mode", "hover"});
    ASSERT_FALSE(needs_variant.has_value());
    EXPECT_NE(needs_variant.error().find("paint, layout"), std::string::npos) << needs_variant.error();

    const auto no_variants = resolve({"--scene", "table", "--mode", "quiet", "--variant", "paint"});
    ASSERT_FALSE(no_variants.has_value());
    EXPECT_NE(no_variants.error().find("no variants"), std::string::npos) << no_variants.error();

    const auto wrong_variant = resolve({"--scene", "paint-mix", "--mode", "quiet", "--variant", "blur"});
    ASSERT_FALSE(wrong_variant.has_value());
    EXPECT_NE(wrong_variant.error().find("nine-slice"), std::string::npos) << wrong_variant.error();
}

TEST(BenchOptions, DefaultReportPathNamesTheCase) {
    EXPECT_EQ(bench::default_report_path({bench::BenchScene::Table, bench::BenchMode::Hover, "paint"}),
            std::filesystem::path("ui_bench-table-hover-paint.json"));
    EXPECT_EQ(bench::default_report_path({bench::BenchScene::Hud, bench::BenchMode::OneChange, {}}),
            std::filesystem::path("ui_bench-hud-one-change.json"));
}

TEST(BenchMatrix, EveryRowIsUniqueAndResolvesByItsName) {
    std::set<std::string> names;
    for (const bench::BenchCase& bench_case : bench::bench_matrix()) {
        const std::string name = bench::bench_case_name(bench_case);
        EXPECT_TRUE(names.insert(name).second) << "twice: " << name;
        bench::BenchOptions options;
        options.scene = bench::bench_scene_name(bench_case.scene);
        options.mode = bench::bench_mode_name(bench_case.mode);
        options.variant = bench_case.variant;
        const auto resolved = bench::resolve_bench_case(options);
        ASSERT_TRUE(resolved.has_value()) << name << ": " << resolved.error();
        EXPECT_EQ(*resolved, bench_case);
    }
    EXPECT_EQ(names.size(), 34u);
}

TEST(BenchMatrix, ReferenceScenesCloseTheBaseline) {
    for (const bench::BenchScene scene :
            {bench::BenchScene::Table, bench::BenchScene::Inspector, bench::BenchScene::Hud}) {
        EXPECT_TRUE(bench::find_bench_case(scene, bench::BenchMode::Quiet, {}));
        EXPECT_TRUE(bench::find_bench_case(scene, bench::BenchMode::OneChange, {}));
        EXPECT_TRUE(bench::find_bench_case(scene, bench::BenchMode::Hover, bench::kHoverPaint));
        EXPECT_TRUE(bench::find_bench_case(scene, bench::BenchMode::Hover, bench::kHoverLayout));
    }
    EXPECT_TRUE(bench::find_bench_case(bench::BenchScene::Table, bench::BenchMode::Churn, {}));
    EXPECT_TRUE(bench::find_bench_case(bench::BenchScene::Clip, bench::BenchMode::Scroll, {}));
    EXPECT_FALSE(bench::find_bench_case(bench::BenchScene::Hud, bench::BenchMode::Churn, {}));
    EXPECT_EQ(bench::bench_variants_of(bench::BenchScene::PaintMix, bench::BenchMode::Quiet).size(), 11u);
    EXPECT_EQ(bench::bench_case_name({bench::BenchScene::PaintMix, bench::BenchMode::Quiet, "nine-slice"}),
            "paint-mix quiet nine-slice");
    EXPECT_EQ(bench::bench_case_name({bench::BenchScene::Text, bench::BenchMode::Scroll, {}}), "text scroll");
}

TEST(BenchSchedule, WarmsUpClearsMeasuresAndFinishes) {
    const bench::BenchSchedule schedule{.warmup = 6, .frames = 3};
    EXPECT_EQ(schedule.at(0, 0), bench::BenchStep::Wait);
    EXPECT_EQ(schedule.at(bench::kSetupTick - 1, 0), bench::BenchStep::Wait);
    EXPECT_EQ(schedule.at(bench::kSetupTick, 0), bench::BenchStep::Setup);
    EXPECT_EQ(schedule.at(bench::kSetupTick + 1, 0), bench::BenchStep::Warmup);
    EXPECT_EQ(schedule.at(5, 0), bench::BenchStep::Warmup);
    EXPECT_EQ(schedule.at(6, 120), bench::BenchStep::Clear) << "the warmup's frames do not count";
    EXPECT_EQ(schedule.at(7, 1), bench::BenchStep::Measure);
    EXPECT_EQ(schedule.at(8, 2), bench::BenchStep::Measure);
    EXPECT_EQ(schedule.at(9, 3), bench::BenchStep::Finish) << "ticks 6, 7, 8 are committed";
    EXPECT_EQ(schedule.at(9 + bench::kGraceTicks, 2), bench::BenchStep::Measure);
    EXPECT_EQ(schedule.at(10 + bench::kGraceTicks, 2), bench::BenchStep::Fail);
    EXPECT_EQ(bench::BenchSchedule::mode_step(bench::kFirstDrivenTick), 0);
}

TEST(FramePlan, EachModeChangesOnlyItsOwnThing) {
    const bench::FramePlan quiet = bench::plan_frame(bench::BenchMode::Quiet, 3, 10);
    EXPECT_FALSE(quiet.change_value);
    EXPECT_FALSE(quiet.move_pointer);
    EXPECT_EQ(quiet.wheel, 0.0f);
    EXPECT_FALSE(quiet.churn);

    const bench::FramePlan one = bench::plan_frame(bench::BenchMode::OneChange, 3, 10);
    EXPECT_TRUE(one.change_value);
    EXPECT_FALSE(one.move_pointer);
    EXPECT_FALSE(one.churn);
    EXPECT_EQ(one.step, 3);

    const bench::FramePlan scroll = bench::plan_frame(bench::BenchMode::Scroll, 3, 10);
    EXPECT_EQ(scroll.wheel, bench::kScrollWheel);
    EXPECT_FALSE(scroll.change_value);

    const bench::FramePlan churn = bench::plan_frame(bench::BenchMode::Churn, 4, 10);
    EXPECT_TRUE(churn.churn);
    EXPECT_EQ(churn.step, 4);
    EXPECT_FALSE(churn.change_value);
}

TEST(FramePlan, HoverLandsOnADifferentTargetEveryFrame) {
    std::size_t previous = 99;
    for (int step = 0; step < 50; ++step) {
        const bench::FramePlan plan = bench::plan_frame(bench::BenchMode::Hover, step, 7);
        ASSERT_TRUE(plan.move_pointer);
        EXPECT_LT(plan.hover_target, 7u);
        EXPECT_NE(plan.hover_target, previous) << "step " << step;
        previous = plan.hover_target;
    }
    EXPECT_FALSE(bench::plan_frame(bench::BenchMode::Hover, 0, 0).move_pointer) << "no targets, no move";
}

TEST(BenchData, RandomIsTheSameSequenceForTheSameSeed) {
    bench::BenchRandom a(42);
    bench::BenchRandom b(42);
    bench::BenchRandom c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const std::uint64_t value = a.next();
        EXPECT_EQ(value, b.next());
        differs = differs || value != c.next();
    }
    EXPECT_TRUE(differs);
    bench::BenchRandom range(7);
    for (int i = 0; i < 1000; ++i) {
        const int value = range.between(-3, 5);
        EXPECT_GE(value, -3);
        EXPECT_LE(value, 5);
        const double unit = range.unit();
        EXPECT_GE(unit, 0.0);
        EXPECT_LT(unit, 1.0);
    }
    EXPECT_EQ(range.between(4, 4), 4);
    EXPECT_EQ(range.between(4, 1), 4);
}

TEST(BenchData, TableRecordsAreDeterministicAndFormatted) {
    const std::vector<bench::TableRecord> records = bench::make_table_records(bench::kTableRows);
    ASSERT_EQ(records.size(), 10000u);
    EXPECT_EQ(records.front().id, 1);
    EXPECT_EQ(records.front().id_text, "00001");
    EXPECT_EQ(records.back().id, 10000);
    EXPECT_EQ(records.back().id_text, "10000");
    for (const bench::TableRecord& record : records) {
        ASSERT_FALSE(record.name.empty());
        ASSERT_FALSE(record.status.empty());
        ASSERT_EQ(record.ratio.size(), 5u) << record.ratio;
        ASSERT_TRUE(record.ratio.starts_with("0.") || record.ratio == "1.000") << record.ratio;
    }
    const std::vector<bench::TableRecord> again = bench::make_table_records(100);
    for (std::size_t i = 0; i < again.size(); ++i) {
        EXPECT_EQ(again[i].name, records[i].name);
        EXPECT_EQ(again[i].value, records[i].value);
        EXPECT_EQ(again[i].ratio, records[i].ratio);
    }
    const std::vector<bench::TableRecord> other = bench::make_table_records(100, 1);
    EXPECT_FALSE(std::ranges::equal(other, again, {}, &bench::TableRecord::value, &bench::TableRecord::value));
}

TEST(BenchData, ParagraphsHaveTheirWordsAndRepeat) {
    const std::vector<std::string> paragraphs = bench::make_paragraphs(3, 12);
    ASSERT_EQ(paragraphs.size(), 3u);
    for (const std::string& paragraph : paragraphs) {
        EXPECT_EQ(std::ranges::count(paragraph, ' '), 11);
        EXPECT_TRUE(paragraph.front() >= 'A' && paragraph.front() <= 'Z') << paragraph;
        EXPECT_EQ(paragraph.back(), '.');
    }
    EXPECT_EQ(bench::make_paragraphs(3, 12), paragraphs);
    bench::BenchRandom random(1);
    EXPECT_TRUE(bench::make_paragraph(random, 0).empty());
}

TEST(BenchData, UnitsStayInsideTheWorld) {
    const std::vector<bench::UnitMarker> units = bench::make_unit_markers(bench::kWorldUnits, bench::kWorldSize);
    ASSERT_EQ(units.size(), bench::kWorldUnits);
    std::set<int> teams;
    for (const bench::UnitMarker& unit : units) {
        EXPECT_GE(unit.position.x, 0.0f);
        EXPECT_LT(unit.position.x, bench::kWorldSize.x);
        EXPECT_GE(unit.position.y, 0.0f);
        EXPECT_LT(unit.position.y, bench::kWorldSize.y);
        teams.insert(unit.team);
    }
    EXPECT_EQ(teams.size(), static_cast<std::size_t>(bench::kTeams));
    const std::vector<bench::UnitMarker> again = bench::make_unit_markers(bench::kWorldUnits, bench::kWorldSize);
    EXPECT_EQ(again.front().position, units.front().position);
    EXPECT_EQ(again.back().team, units.back().team);
}

TEST(BenchReport, WrapsTheProfileWithTheRunsMetadata) {
    bench::BenchReport report;
    report.bench_case = {bench::BenchScene::Table, bench::BenchMode::Hover, "paint"};
    report.warmup = 60;
    report.frames = 120;
    report.width = 1600;
    report.height = 900;
    report.drawable_width = 1600;
    report.drawable_height = 900;
    report.hover_targets = 33;
    report.config = "RelWithDebInfo";
    report.engine_version = "0.1.0";
    report.build_id = "abc";
    report.commit = "deadbeef";
    report.dirty = true;
    report.timestamp = "2026-10-09T12:00:00Z";
    const std::string json = bench::bench_report_json(report, R"({"paused":false})");
    EXPECT_EQ(json, "{\"bench\":{\"scene\":\"table\",\"mode\":\"hover\",\"variant\":\"paint\",\"warmup\":60,"
                    "\"frames\":120,\"window\":{\"width\":1600,\"height\":900,\"drawable_width\":1600,"
                    "\"drawable_height\":900},\"vsync\":false,\"hover_targets\":33,\"config\":\"RelWithDebInfo\","
                    "\"engine_version\":\"0.1.0\",\"build_id\":\"abc\",\"commit\":\"deadbeef\",\"dirty\":true,"
                    "\"timestamp\":\"2026-10-09T12:00:00Z\"},\"profile\":{\"paused\":false}}\n");

    report.commit = "a\"b\\c";
    const std::string escaped = bench::bench_report_json(report, "");
    EXPECT_NE(escaped.find(R"("commit":"a\"b\\c")"), std::string::npos) << escaped;
    EXPECT_NE(escaped.find(R"("profile":null})"), std::string::npos) << escaped;
}

TEST(BenchReport, TimestampIsUtcIso8601) {
    using namespace std::chrono;
    const sys_seconds time = sys_days{year{2026} / October / 9} + hours{14} + minutes{3} + seconds{27};
    EXPECT_EQ(bench::utc_timestamp(time + milliseconds{600}), "2026-10-09T14:03:27Z");
}

TEST(ScenePoints, FindsTheRestPointAndTheVisibleHoverTargets) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas>)"
                                        R"(<Label id="rest" text="Title"/>)"
                                        R"(<Button class="hot" content="A"/>)"
                                        R"(<Button class="hot" content="B"/>)"
                                        R"(<Button class="hot" content="Off screen"/>)"
                                        R"(<Stack class="hot"/>)"
                                        R"(<Button class="hot" content="Under"/>)"
                                        R"(<Button content="Over"/>)"
                                        R"(</Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::Element& root = parsed->root;
    ASSERT_EQ(root.children.size(), 7u);
    root.layout_rect = {0.0f, 0.0f, 400.0f, 300.0f};
    root.children[0].layout_rect = {10.0f, 10.0f, 100.0f, 20.0f};
    root.children[1].layout_rect = {10.0f, 40.0f, 80.0f, 20.0f};
    root.children[2].layout_rect = {100.0f, 40.0f, 80.0f, 20.0f};
    root.children[3].layout_rect = {10.0f, 500.0f, 80.0f, 20.0f};
    root.children[4].layout_rect = {10.0f, 80.0f, 80.0f, 20.0f};
    root.children[5].layout_rect = {200.0f, 200.0f, 80.0f, 20.0f};
    root.children[6].layout_rect = {200.0f, 200.0f, 80.0f, 20.0f};
    const engine::render::Rect window{0.0f, 0.0f, 400.0f, 300.0f};

    const std::optional<glm::vec2> rest = bench::element_point(root, "rest", window);
    ASSERT_TRUE(rest.has_value());
    EXPECT_EQ(*rest, glm::vec2(60.0f, 20.0f));
    EXPECT_FALSE(bench::element_point(root, "missing", window).has_value());

    // Only the part inside the window counts.
    const std::optional<glm::vec2> clipped = bench::element_point(root, "rest", {0.0f, 0.0f, 30.0f, 300.0f});
    ASSERT_TRUE(clipped.has_value());
    EXPECT_EQ(*clipped, glm::vec2(20.0f, 20.0f));

    // The off-screen button, the Stack nothing can hit, and the button under another are left out.
    const std::vector<glm::vec2> points = bench::hover_points(root, window);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0], glm::vec2(50.0f, 50.0f));
    EXPECT_EQ(points[1], glm::vec2(140.0f, 50.0f));
}

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}

// The asset loader drops CSS warnings, so a property the engine does not know would be silent in a run.
TEST(BenchAssets, EveryStylesheetParsesWithoutWarnings) {
    int sheets = 0;
    const std::filesystem::path directory = std::filesystem::path(WIND_BENCH_ASSETS_DIR) / "css";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".css") {
            continue;
        }
        ++sheets;
        std::vector<std::string> warnings;
        const auto sheet = engine::ui::parse_css(read_text(entry.path()), warnings);
        ASSERT_TRUE(sheet.has_value()) << entry.path();
        EXPECT_TRUE(warnings.empty()) << entry.path() << ": " << (warnings.empty() ? "" : warnings.front());
        EXPECT_FALSE(sheet->rules.empty() && sheet->keyframes.empty()) << entry.path();
    }
    EXPECT_EQ(sheets, 20);
}

TEST(BenchAssets, EveryDocumentParses) {
    int documents = 0;
    const std::filesystem::path directory = std::filesystem::path(WIND_BENCH_ASSETS_DIR) / "ui";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".xml") {
            continue;
        }
        ++documents;
        auto document = engine::ui::parse_xml(read_text(entry.path()));
        EXPECT_TRUE(document.has_value()) << entry.path();
        if (document) {
            EXPECT_NE(engine::ui::find_by_id(document->root, "rest"), nullptr)
                    << entry.path() << " has no rest point";
        }
    }
    EXPECT_EQ(documents, 10);
}
