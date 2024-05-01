#include <gtest/gtest.h>

#include <engine/render/graphic_factory.h>

#include "resources/icon_codegen.h"
#include "resources/importers.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using engine::IconCodegenErrorKind;
using engine::render::TextureDesc;

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_icon_codegen_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

TextureDesc make_gradient(int width, int height) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
            desc.rgba[i + 0] = static_cast<std::uint8_t>(x % 256);
            desc.rgba[i + 1] = static_cast<std::uint8_t>(y % 256);
            desc.rgba[i + 2] = 128;
            desc.rgba[i + 3] = 255;
        }
    }
    return desc;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::uint16_t read_u16le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t read_u32le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t read_u32be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

}

TEST(IconCodegen, RejectsNonSquareInput) {
    TempDir dir;
    const TextureDesc master = make_gradient(1024, 512);
    write_file(dir.path / "input.png", engine::icon_encode_png(master));

    const auto result = engine::icon_codegen_write((dir.path / "input.png").string(), (dir.path / "out").string());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, IconCodegenErrorKind::NotSquare);
}

TEST(IconCodegen, RejectsSmallerThanMinimum) {
    TempDir dir;
    const TextureDesc master = make_gradient(512, 512);
    write_file(dir.path / "input.png", engine::icon_encode_png(master));

    const auto result = engine::icon_codegen_write((dir.path / "input.png").string(), (dir.path / "out").string());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, IconCodegenErrorKind::TooSmall);
}

TEST(IconCodegen, RejectsUndecodablePng) {
    TempDir dir;
    write_file(dir.path / "input.png", {0x00, 0x01, 0x02, 0x03});

    const auto result = engine::icon_codegen_write((dir.path / "input.png").string(), (dir.path / "out").string());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, IconCodegenErrorKind::Decode);
}

TEST(IconCodegen, IcoContainsExpectedEntries) {
    const TextureDesc master = make_gradient(256, 256);
    constexpr std::array<int, 4> sizes{16, 32, 48, 256};
    const std::vector<std::uint8_t> ico = engine::icon_encode_ico(master, sizes);

    ASSERT_GE(ico.size(), 6u);
    EXPECT_EQ(read_u16le(ico, 0), 0u);                                    // reserved
    EXPECT_EQ(read_u16le(ico, 2), 1u);                                    // type = icon
    EXPECT_EQ(read_u16le(ico, 4), static_cast<std::uint16_t>(sizes.size())); // count

    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const std::size_t entry_offset = 6 + i * 16;
        const int size = sizes[i];
        const std::uint8_t expected_dim = size >= 256 ? 0 : static_cast<std::uint8_t>(size);
        EXPECT_EQ(ico[entry_offset + 0], expected_dim) << "width at entry " << i;
        EXPECT_EQ(ico[entry_offset + 1], expected_dim) << "height at entry " << i;
        EXPECT_EQ(read_u16le(ico, entry_offset + 4), 1u) << "planes at entry " << i;
        EXPECT_EQ(read_u16le(ico, entry_offset + 6), 32u) << "bitCount at entry " << i;

        const std::uint32_t bytes_in_res = read_u32le(ico, entry_offset + 8);
        const std::uint32_t image_offset = read_u32le(ico, entry_offset + 12);
        ASSERT_LE(static_cast<std::uint64_t>(image_offset) + bytes_in_res, ico.size());

        const std::string blob(reinterpret_cast<const char*>(ico.data() + image_offset), bytes_in_res);
        const std::optional<TextureDesc> decoded = engine::decode_png_rgba(blob);
        ASSERT_TRUE(decoded.has_value()) << "entry " << i << " PNG blob failed to decode";
        EXPECT_EQ(decoded->width, size) << "entry " << i;
        EXPECT_EQ(decoded->height, size) << "entry " << i;
    }
}

TEST(IconCodegen, IcnsHeaderAndChunksAreConsistent) {
    const TextureDesc master = make_gradient(1024, 1024);
    const std::vector<std::uint8_t> icns = engine::icon_encode_icns(master);

    ASSERT_GE(icns.size(), 8u);
    EXPECT_EQ(icns[0], 'i');
    EXPECT_EQ(icns[1], 'c');
    EXPECT_EQ(icns[2], 'n');
    EXPECT_EQ(icns[3], 's');
    EXPECT_EQ(read_u32be(icns, 4), static_cast<std::uint32_t>(icns.size()));

    struct ExpectedSlot {
        const char* tag;
        int size;
    };
    constexpr std::array<ExpectedSlot, 7> kExpected{{
            {"icp4", 16},
            {"icp5", 32},
            {"icp6", 48},
            {"ic07", 128},
            {"ic08", 256},
            {"ic09", 512},
            {"ic10", 1024},
    }};

    std::size_t offset = 8;
    std::size_t slot_index = 0;
    while (offset < icns.size()) {
        ASSERT_LE(offset + 8, icns.size()) << "truncated chunk header at " << offset;
        const std::string tag(reinterpret_cast<const char*>(icns.data() + offset), 4);
        const std::uint32_t chunk_len = read_u32be(icns, offset + 4);
        ASSERT_GE(chunk_len, 8u) << "chunk length must include its own header";
        ASSERT_LE(offset + chunk_len, icns.size()) << "chunk overruns file at tag " << tag;

        ASSERT_LT(slot_index, kExpected.size());
        EXPECT_EQ(tag, kExpected[slot_index].tag);
        const std::string blob(reinterpret_cast<const char*>(icns.data() + offset + 8), chunk_len - 8);
        const std::optional<TextureDesc> decoded = engine::decode_png_rgba(blob);
        ASSERT_TRUE(decoded.has_value()) << "chunk " << tag << " failed to decode";
        EXPECT_EQ(decoded->width, kExpected[slot_index].size) << "chunk " << tag;
        EXPECT_EQ(decoded->height, kExpected[slot_index].size) << "chunk " << tag;

        offset += chunk_len;
        ++slot_index;
    }
    EXPECT_EQ(offset, icns.size()) << "chunks must exactly cover the file with no overrun or gap";
    EXPECT_EQ(slot_index, kExpected.size());
}

TEST(IconCodegen, HappyPathWritesAllOutputs) {
    TempDir dir;
    const TextureDesc master = make_gradient(1024, 1024);
    write_file(dir.path / "input.png", engine::icon_encode_png(master));

    const std::filesystem::path out_dir = dir.path / "out";
    const auto result = engine::icon_codegen_write((dir.path / "input.png").string(), out_dir.string());
    ASSERT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().message);

    {
        const std::vector<std::uint8_t> ico = read_file(out_dir / "icon.ico");
        ASSERT_GE(ico.size(), 6u);
        EXPECT_EQ(read_u16le(ico, 2), 1u);
        EXPECT_EQ(read_u16le(ico, 4), 4u);
    }
    {
        const std::vector<std::uint8_t> icns = read_file(out_dir / "icon.icns");
        ASSERT_GE(icns.size(), 8u);
        EXPECT_EQ(read_u32be(icns, 4), static_cast<std::uint32_t>(icns.size()));
    }

    struct Mipmap {
        const char* dir;
        int size;
    };
    constexpr std::array<Mipmap, 5> kMipmaps{{
            {"mipmap-mdpi", 48},
            {"mipmap-hdpi", 72},
            {"mipmap-xhdpi", 96},
            {"mipmap-xxhdpi", 144},
            {"mipmap-xxxhdpi", 192},
    }};
    for (const Mipmap& mipmap : kMipmaps) {
        const std::filesystem::path png_path =
                out_dir / mipmap.dir / "ic_launcher.png";
        ASSERT_TRUE(std::filesystem::exists(png_path)) << png_path.string();
        const std::vector<std::uint8_t> bytes = read_file(png_path);
        const std::string blob(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const std::optional<TextureDesc> decoded = engine::decode_png_rgba(blob);
        ASSERT_TRUE(decoded.has_value()) << mipmap.dir;
        EXPECT_EQ(decoded->width, mipmap.size) << mipmap.dir;
        EXPECT_EQ(decoded->height, mipmap.size) << mipmap.dir;
    }

    {
        const std::filesystem::path favicon_path = out_dir / "favicon.png";
        ASSERT_TRUE(std::filesystem::exists(favicon_path));
        const std::vector<std::uint8_t> bytes = read_file(favicon_path);
        const std::string blob(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const std::optional<TextureDesc> decoded = engine::decode_png_rgba(blob);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->width, 256);
        EXPECT_EQ(decoded->height, 256);
    }
}
