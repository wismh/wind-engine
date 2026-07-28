#include <gtest/gtest.h>

#include "dock_layout_file.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>

namespace {

using engine::ui::DockLayout;

class LayoutDir {
public:
    LayoutDir()
        : root_(std::filesystem::temp_directory_path() / "wind_dock_layout_file_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
    }
    ~LayoutDir() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    LayoutDir(const LayoutDir&) = delete;
    LayoutDir& operator=(const LayoutDir&) = delete;

    [[nodiscard]] std::filesystem::path file() const { return root_ / "nested" / "dock_layout.toml"; }

private:
    std::filesystem::path root_;
};

DockLayout two_panels() {
    DockLayout layout;
    layout.add("project", {});
    layout.add("build", {layout.find("project")->stack, engine::ui::DockZone::Bottom});
    layout.float_panel("build", engine::render::Rect{10.0f, 20.0f, 300.5f, 200.25f});
    return layout;
}

}

TEST(DockLayoutFile, AMissingFileIsNoLayout) {
    const LayoutDir dir;
    EXPECT_FALSE(editor::DockLayoutFile{dir.file()}.load().has_value());
}

TEST(DockLayoutFile, SaveCreatesTheDirectoryAndLoadReadsTheSameLayout) {
    const LayoutDir dir;
    const editor::DockLayoutFile file{dir.file()};
    const DockLayout layout = two_panels();
    ASSERT_TRUE(file.save(layout));
    EXPECT_FALSE(std::filesystem::exists(dir.file().string() + ".tmp"));
    const std::optional<DockLayout> loaded = file.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, layout);

    // Saving again replaces the file.
    DockLayout other;
    other.add("inspector", {});
    ASSERT_TRUE(file.save(other));
    EXPECT_EQ(file.load(), other);
}

TEST(DockLayoutFile, ACorruptFileIsNoLayout) {
    const LayoutDir dir;
    std::filesystem::create_directories(dir.file().parent_path());
    std::ofstream(dir.file(), std::ios::binary) << "this is not toml [[[";
    EXPECT_FALSE(editor::DockLayoutFile{dir.file()}.load().has_value());
    std::ofstream(dir.file(), std::ios::binary | std::ios::trunc) << "version = 99\n";
    EXPECT_FALSE(editor::DockLayoutFile{dir.file()}.load().has_value());
}

TEST(DockLayoutFile, AnEmptyPathReadsAndWritesNothing) {
    const editor::DockLayoutFile file;
    EXPECT_TRUE(file.path().empty());
    EXPECT_FALSE(file.load().has_value());
    EXPECT_FALSE(file.save(two_panels()));
}
