#include <gtest/gtest.h>

#include "asset_inspection.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// A directory per test with the files a test writes.
class FileDir {
public:
    FileDir()
        : root_(std::filesystem::temp_directory_path() / "wind_asset_inspection_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    ~FileDir() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    FileDir(const FileDir&) = delete;
    FileDir& operator=(const FileDir&) = delete;

    void write(const std::string& name, std::string_view bytes) const {
        std::ofstream(root_ / name, std::ios::binary) << bytes;
    }

    // What the Project tab would select for `name`.
    [[nodiscard]] editor::AssetSelection file(const std::string& name) const {
        return editor::AssetSelection{.key = "assets/" + name,
                .path = root_ / name,
                .size = std::filesystem::file_size(root_ / name)};
    }

private:
    std::filesystem::path root_;
};

const editor::InspectorSection* section(const std::vector<editor::InspectorSection>& sections,
        std::string_view heading) {
    for (const editor::InspectorSection& candidate : sections) {
        if (candidate.heading == heading) {
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<std::string> headings(const std::vector<editor::InspectorSection>& sections) {
    std::vector<std::string> out;
    for (const editor::InspectorSection& candidate : sections) {
        out.push_back(candidate.heading);
    }
    return out;
}

}

TEST(AssetInspection, AFolderShowsItsPathAndItems) {
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(
            editor::AssetSelection{.key = "assets/ui", .path = "unused", .directory = true, .items = 3});
    ASSERT_EQ(sections.size(), 1u);
    EXPECT_EQ(sections[0], (editor::InspectorSection{.heading = "Folder", .lines = {"Path: assets/ui", "Items: 3"}}));
}

TEST(AssetInspection, ATextFileShowsItsLinesAndItsMeta) {
    const FileDir dir;
    dir.write("menu.xml", "<Canvas>\r\n\t<Label/>\r\n</Canvas>\r\n");
    dir.write("menu.xml.meta", "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"ui\"\n");
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("menu.xml"));
    EXPECT_EQ(headings(sections), (std::vector<std::string>{"File", "Import", "Content"}));
    EXPECT_EQ(sections[0].lines, (std::vector<std::string>{"Path: assets/menu.xml", "Size: 32 bytes"}));
    EXPECT_EQ(sections[1].lines,
            (std::vector<std::string>{"GUID: 0123456789abcdef0123456789abcdef", "Importer: ui"}));
    EXPECT_EQ(sections[2].lines, (std::vector<std::string>{"<Canvas>", "    <Label/>", "</Canvas>"}))
            << "CR dropped, a tab is 4 spaces";
}

TEST(AssetInspection, TextureAndAudioMetaShowTheirSettings) {
    const FileDir dir;
    dir.write("hero.png", "not really a png");
    dir.write("hero.png.meta", "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"texture\"\nfilter = \"nearest\"\n");
    const std::optional<editor::InspectorSection> texture = [&] {
        const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("hero.png"));
        const editor::InspectorSection* found = section(sections, "Import");
        return found != nullptr ? std::optional{*found} : std::nullopt;
    }();
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->lines[1], "Importer: texture");
    EXPECT_NE(std::ranges::find(texture->lines, "Filter: nearest"), texture->lines.end());
    EXPECT_NE(std::ranges::find(texture->lines, "Pixels per unit: 100"), texture->lines.end());

    dir.write("hit.wav", std::string("RIFF\0\0\0\0WAVE", 12));
    dir.write("hit.wav.meta",
            "guid = \"0123456789abcdef0123456789abcdee\"\nimporter = \"audio\"\nbank = \"music\"\nloop = true\n");
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("hit.wav"));
    EXPECT_EQ(headings(sections), (std::vector<std::string>{"File", "Import"})) << "binary: no Content";
    EXPECT_NE(std::ranges::find(sections[1].lines, "Bank: music"), sections[1].lines.end());
    EXPECT_NE(std::ranges::find(sections[1].lines, "Loop: yes"), sections[1].lines.end());
}

TEST(AssetInspection, ABrokenMetaSaysWhy) {
    const FileDir dir;
    dir.write("a.css", "Label {}");
    dir.write("a.css.meta", "guid = \"short\"\nimporter = \"css\"\n");
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("a.css"));
    const editor::InspectorSection* import = section(sections, "Import");
    ASSERT_NE(import, nullptr);
    EXPECT_EQ(import->lines, (std::vector<std::string>{"The .meta does not read: its guid is not 32 hex digits."}));
}

TEST(AssetInspection, APngShowsItsSize) {
    const FileDir dir;
    // Signature, then the IHDR chunk: length 13, "IHDR", width 300, height 2.
    const std::string png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\x01\x2c\0\0\0\x02\x08\x06\0\0\0", 29);
    dir.write("icon.png", png);
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("icon.png"));
    EXPECT_EQ(headings(sections), (std::vector<std::string>{"File", "Content"})) << "no .meta, no Import";
    EXPECT_EQ(sections[1].lines, (std::vector<std::string>{"PNG image, 300 x 2"}));
}

TEST(AssetInspection, ALongTextIsCutAndSaysSo) {
    const FileDir dir;
    std::string text;
    for (std::size_t i = 0; i < editor::kMaxContentLines + 20; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    dir.write("long.txt", text);
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(dir.file("long.txt"));
    const editor::InspectorSection* content = section(sections, "Content");
    ASSERT_NE(content, nullptr);
    ASSERT_EQ(content->lines.size(), editor::kMaxContentLines + 1);
    EXPECT_EQ(content->lines.front(), "line 0");
    EXPECT_EQ(content->lines[editor::kMaxContentLines - 1], "line 499");
    EXPECT_EQ(content->lines.back().find("... the first 500 lines of"), 0u) << content->lines.back();

    // Past the byte limit: the partial last line is left out.
    dir.write("wide.txt", std::string(editor::kMaxContentBytes + 10, 'x'));
    const std::vector<editor::InspectorSection> wide = editor::inspect_asset(dir.file("wide.txt"));
    ASSERT_NE(section(wide, "Content"), nullptr);
    EXPECT_EQ(section(wide, "Content")->lines, (std::vector<std::string>{"... the first 0 lines of 64.0 KB"}));
}

TEST(AssetInspection, AnEmptyFileAndAGoneFile) {
    const FileDir dir;
    dir.write("empty.txt", "");
    const std::vector<editor::InspectorSection> empty = editor::inspect_asset(dir.file("empty.txt"));
    ASSERT_NE(section(empty, "Content"), nullptr);
    EXPECT_EQ(section(empty, "Content")->lines, (std::vector<std::string>{"Empty"}));

    editor::AssetSelection gone = dir.file("empty.txt");
    gone.path = gone.path.parent_path() / "gone.txt";
    const std::vector<editor::InspectorSection> sections = editor::inspect_asset(gone);
    EXPECT_EQ(headings(sections), (std::vector<std::string>{"File"}));
    EXPECT_EQ(sections[0].lines.back(), "gone.txt does not open.");
}
