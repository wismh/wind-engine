#include <gtest/gtest.h>

#include "thumbnail_cache.h"

#include <engine/resources/assets_db.h>
#include <engine/resources/image_io.h>

#include "fixtures/fatal_fakes.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr char kGuid[] = "0123456789abcdef0123456789abcdef";

engine::render::TextureDesc solid(int width, int height) {
    engine::render::TextureDesc image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 200u);
    return image;
}

// A project directory whose `assets/` holds the textures a test writes.
class Project {
public:
    Project()
        : root_(std::filesystem::temp_directory_path() / "wind_thumbnail_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "assets");
    }

    ~Project() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    void write_png(const std::string& name, const engine::render::TextureDesc& image, bool with_meta = true) const {
        const std::vector<std::uint8_t> bytes = engine::encode_png_rgba(image);
        std::ofstream(root_ / "assets" / name, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (with_meta) {
            std::ofstream(root_ / "assets" / (name + ".meta"))
                    << "guid = \"" << kGuid << "\"\nimporter = \"texture\"\n";
        }
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] std::filesystem::path png(const std::string& name) const { return root_ / "assets" / name; }

    [[nodiscard]] editor::ThumbnailStamp stamp(const std::string& name) const {
        return editor::ThumbnailStamp{std::filesystem::file_size(png(name)),
                static_cast<std::int64_t>(std::filesystem::last_write_time(png(name)).time_since_epoch().count())};
    }

private:
    std::filesystem::path root_;
};

// Polls until `done` is true, for about five seconds.
template<typename Done>
bool wait_for(Done&& done) {
    for (int i = 0; i < 500; ++i) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

TEST(ThumbnailCache, FileNameCarriesGuidAndStamp) {
    const editor::ThumbnailStamp stamp{1234, 99};
    const std::filesystem::path file = editor::thumbnail_cache_file("proj", engine::AssetId{kGuid}, stamp);
    EXPECT_EQ(file.filename().string(), std::string(kGuid) + ".1234.99.png");
    EXPECT_EQ(file.parent_path().filename().string(), "01");
    EXPECT_NE(file.string().find(".wind"), std::string::npos);
}

TEST(ThumbnailCache, UiIdChangesWithTheStamp) {
    const engine::AssetId guid{kGuid};
    const engine::AssetId id = editor::thumbnail_ui_id(guid, {10, 1});
    EXPECT_EQ(id, editor::thumbnail_ui_id(guid, {10, 1}));
    EXPECT_NE(id, editor::thumbnail_ui_id(guid, {10, 2}));
    EXPECT_NE(id, editor::thumbnail_ui_id(guid, {11, 1}));
    EXPECT_NE(id, editor::preview_ui_id("assets/hero.png", {10, 1}));
}

TEST(ThumbnailCache, BuildScalesKeepsASquareAndWritesTheCache) {
    Project project;
    project.write_png("hero.png", solid(512, 256));
    const editor::ThumbnailStamp stamp = project.stamp("hero.png");

    const auto made = editor::build_thumbnail(project.root(), project.png("hero.png"), stamp);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->guid, engine::AssetId{kGuid});
    EXPECT_EQ(made->image.width, editor::kThumbnailEdge);
    EXPECT_EQ(made->image.height, editor::kThumbnailEdge);
    // The 2:1 picture sits in the middle band: the top row is transparent, the middle row is not.
    EXPECT_EQ(made->image.rgba[3], 0u);
    const std::size_t middle = static_cast<std::size_t>(editor::kThumbnailEdge / 2) * editor::kThumbnailEdge * 4u + 3u;
    EXPECT_EQ(made->image.rgba[middle], 200u);

    EXPECT_TRUE(std::filesystem::exists(editor::thumbnail_cache_file(project.root(), made->guid, stamp)));
    EXPECT_TRUE(std::filesystem::exists(project.root() / ".wind" / ".gitignore"));
}

TEST(ThumbnailCache, SecondBuildReadsTheCacheNotTheSource) {
    Project project;
    project.write_png("hero.png", solid(300, 300));
    const editor::ThumbnailStamp stamp = project.stamp("hero.png");
    ASSERT_TRUE(editor::build_thumbnail(project.root(), project.png("hero.png"), stamp).has_value());

    // The source is gone, but a cache file for this stamp answers.
    std::filesystem::remove(project.png("hero.png"));
    const auto again = editor::build_thumbnail(project.root(), project.png("hero.png"), stamp);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->image.width, editor::kThumbnailEdge);
}

TEST(ThumbnailCache, ANewStampReplacesTheOldCacheFile) {
    Project project;
    project.write_png("hero.png", solid(300, 300));
    const editor::ThumbnailStamp first = project.stamp("hero.png");
    ASSERT_TRUE(editor::build_thumbnail(project.root(), project.png("hero.png"), first).has_value());

    const editor::ThumbnailStamp second{first.size + 1, first.modified + 1};
    ASSERT_TRUE(editor::build_thumbnail(project.root(), project.png("hero.png"), second).has_value());
    EXPECT_FALSE(std::filesystem::exists(editor::thumbnail_cache_file(project.root(), engine::AssetId{kGuid}, first)));
    EXPECT_TRUE(std::filesystem::exists(editor::thumbnail_cache_file(project.root(), engine::AssetId{kGuid}, second)));
}

TEST(ThumbnailCache, NoSidecarAndNoPngGiveNothing) {
    Project project;
    project.write_png("bare.png", solid(8, 8), false);
    EXPECT_FALSE(editor::build_thumbnail(project.root(), project.png("bare.png"), project.stamp("bare.png")));

    std::ofstream(project.png("fake.png")) << "not a png";
    std::ofstream(project.png("fake.png.meta")) << "guid = \"" << kGuid << "\"\nimporter = \"texture\"\n";
    EXPECT_FALSE(editor::build_thumbnail(project.root(), project.png("fake.png"), {9, 1}));
}

TEST(ThumbnailCache, PreviewFitsTheLongerSide) {
    Project project;
    project.write_png("hero.png", solid(1024, 512), false);
    const auto preview = editor::build_preview(project.png("hero.png"));
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->width, editor::kPreviewEdge);
    EXPECT_EQ(preview->height, editor::kPreviewEdge / 2);
}

TEST(ThumbnailService, DeliversAThumbnailAsARuntimeTexture) {
    Project project;
    project.write_png("hero.png", solid(256, 256));
    const editor::ThumbnailStamp stamp = project.stamp("hero.png");

    fakes::QuietFatal fatal;
    engine::AssetsDb assets(fatal);
    editor::ThumbnailService service;
    service.open(project.root());

    EXPECT_FALSE(service.find_or_request("assets/hero.png", project.png("hero.png"), stamp).has_value());
    ASSERT_TRUE(wait_for([&] { return service.poll(assets); }));

    const auto id = service.find_or_request("assets/hero.png", project.png("hero.png"), stamp);
    ASSERT_TRUE(id.has_value());
    ASSERT_NE(assets.runtime_texture(*id), nullptr);
    EXPECT_EQ(assets.runtime_texture(*id)->width, editor::kThumbnailEdge);
    EXPECT_EQ(service.ready_count(), 1u);
}

TEST(ThumbnailService, ChangedStampIsRequestedAgainUnderANewId) {
    Project project;
    project.write_png("hero.png", solid(256, 256));
    editor::ThumbnailStamp stamp = project.stamp("hero.png");

    fakes::QuietFatal fatal;
    engine::AssetsDb assets(fatal);
    editor::ThumbnailService service;
    service.open(project.root());
    (void) service.find_or_request("assets/hero.png", project.png("hero.png"), stamp);
    ASSERT_TRUE(wait_for([&] { return service.poll(assets); }));
    const auto first = service.find_or_request("assets/hero.png", project.png("hero.png"), stamp);
    ASSERT_TRUE(first.has_value());

    project.write_png("hero.png", solid(300, 300));
    stamp.modified += 1;
    EXPECT_FALSE(service.find_or_request("assets/hero.png", project.png("hero.png"), stamp).has_value());
    ASSERT_TRUE(wait_for([&] { return service.poll(assets); }));
    const auto second = service.find_or_request("assets/hero.png", project.png("hero.png"), stamp);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(service.ready_count(), 1u);
}

TEST(ThumbnailService, FailedFileIsNotAskedForAgain) {
    Project project;
    project.write_png("bare.png", solid(8, 8), false);
    const editor::ThumbnailStamp stamp = project.stamp("bare.png");

    fakes::QuietFatal fatal;
    engine::AssetsDb assets(fatal);
    editor::ThumbnailService service;
    service.open(project.root());
    EXPECT_FALSE(service.find_or_request("assets/bare.png", project.png("bare.png"), stamp).has_value());
    ASSERT_TRUE(wait_for([&] { return service.pending_count() == 0; }));
    (void) service.poll(assets);
    EXPECT_FALSE(service.find_or_request("assets/bare.png", project.png("bare.png"), stamp).has_value());
    EXPECT_EQ(service.pending_count(), 0u);
    EXPECT_EQ(service.ready_count(), 0u);
}

TEST(ThumbnailService, PreviewReportsItsSize) {
    Project project;
    project.write_png("wide.png", solid(1000, 500), false);
    const editor::ThumbnailStamp stamp = project.stamp("wide.png");

    fakes::QuietFatal fatal;
    engine::AssetsDb assets(fatal);
    editor::ThumbnailService service;
    service.open(project.root());
    EXPECT_FALSE(service.find_or_request_preview("assets/wide.png", project.png("wide.png"), stamp).has_value());
    ASSERT_TRUE(wait_for([&] { return service.poll(assets); }));
    const auto preview = service.find_or_request_preview("assets/wide.png", project.png("wide.png"), stamp);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->width, editor::kPreviewEdge);
    EXPECT_EQ(preview->height, editor::kPreviewEdge / 2);
    EXPECT_NE(assets.runtime_texture(preview->id), nullptr);
}

TEST(ThumbnailService, ClosedServiceAsksForNothing) {
    Project project;
    project.write_png("hero.png", solid(16, 16));
    editor::ThumbnailService service;
    EXPECT_FALSE(service.find_or_request("assets/hero.png", project.png("hero.png"), project.stamp("hero.png")));
    service.open(project.root());
    service.close();
    EXPECT_FALSE(service.find_or_request("assets/hero.png", project.png("hero.png"), project.stamp("hero.png")));
    EXPECT_EQ(service.pending_count(), 0u);
}

}
