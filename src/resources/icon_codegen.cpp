#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4456)
#pragma warning(disable : 4505)
#pragma warning(disable : 4100)
#endif

#include "stb_image_resize2.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4456)
#pragma warning(disable : 4505)
#pragma warning(disable : 4996)
#endif

#include "stb_image_write.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "icon_codegen.h"

#include "importers.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace engine {
namespace {

constexpr std::array<int, 4> kIcoSizes{16, 32, 48, 256};

struct IcnsSlot {
    const char* tag;
    int size;
};

// OSType tags for the plain-PNG icon family, verified against the Apple Icon Image format
// reference table (icp4/icp5/icp6 since 10.7, ic07-ic10 since 10.5/10.7) rather than recalled
// from memory — a wrong 4-byte tag silently produces an .icns Finder/iconutil can't read.
constexpr std::array<IcnsSlot, 7> kIcnsSlots{{
        {"icp4", 16},
        {"icp5", 32},
        {"icp6", 48},
        {"ic07", 128},
        {"ic08", 256},
        {"ic09", 512},
        {"ic10", 1024},
}};

struct AndroidMipmap {
    const char* dir;
    int size;
};

constexpr std::array<AndroidMipmap, 5> kAndroidMipmaps{{
        {"mipmap-mdpi", 48},
        {"mipmap-hdpi", 72},
        {"mipmap-xhdpi", 96},
        {"mipmap-xxhdpi", 144},
        {"mipmap-xxxhdpi", 192},
}};

constexpr int kFaviconSize = 256;

void write_u16le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void write_u32le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void write_u32be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

std::expected<void, IconCodegenError> write_binary_file(
        const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code dir_ec;
    std::filesystem::create_directories(path.parent_path(), dir_ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(IconCodegenError{IconCodegenErrorKind::Io, "failed to write " + path.generic_string()});
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return std::unexpected(IconCodegenError{IconCodegenErrorKind::Io, "failed to write " + path.generic_string()});
    }
    return {};
}

}

render::TextureDesc icon_resize_rgba(const render::TextureDesc& src, int size) {
    render::TextureDesc dst;
    dst.width = size;
    dst.height = size;
    dst.filter = src.filter;
    dst.wrap = src.wrap;
    dst.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u, 0u);

    stbir_resize_uint8_linear(src.rgba.data(), src.width, src.height, 0, dst.rgba.data(), size, size, 0, STBIR_RGBA);
    return dst;
}

std::vector<std::uint8_t> icon_encode_png(const render::TextureDesc& image) {
    int out_len = 0;
    unsigned char* png = stbi_write_png_to_mem(
            image.rgba.data(), 0, image.width, image.height, 4, &out_len);
    if (png == nullptr || out_len <= 0) {
        return {};
    }
    std::vector<std::uint8_t> out(png, png + out_len);
    std::free(png);
    return out;
}

std::vector<std::uint8_t> icon_encode_ico(const render::TextureDesc& master, std::span<const int> sizes) {
    std::vector<std::vector<std::uint8_t>> pngs;
    pngs.reserve(sizes.size());
    for (const int size : sizes) {
        pngs.push_back(icon_encode_png(icon_resize_rgba(master, size)));
    }

    std::vector<std::uint8_t> out;
    write_u16le(out, 0);                                    // ICONDIR.reserved
    write_u16le(out, 1);                                    // ICONDIR.type = icon
    write_u16le(out, static_cast<std::uint16_t>(sizes.size())); // ICONDIR.count

    std::uint32_t offset = static_cast<std::uint32_t>(6 + 16 * sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const int size = sizes[i];
        const auto dim = static_cast<std::uint8_t>(size >= 256 ? 0 : size); // 0 means 256 in ICONDIRENTRY
        out.push_back(dim);                                 // width
        out.push_back(dim);                                 // height
        out.push_back(0);                                   // colorCount
        out.push_back(0);                                   // reserved
        write_u16le(out, 1);                                // planes
        write_u16le(out, 32);                                // bitCount
        write_u32le(out, static_cast<std::uint32_t>(pngs[i].size())); // bytesInRes
        write_u32le(out, offset);                            // imageOffset
        offset += static_cast<std::uint32_t>(pngs[i].size());
    }
    for (const std::vector<std::uint8_t>& png : pngs) {
        out.insert(out.end(), png.begin(), png.end());
    }
    return out;
}

std::vector<std::uint8_t> icon_encode_icns(const render::TextureDesc& master) {
    std::vector<std::uint8_t> out;
    write_u32be(out, 0x69636e73u); // 'icns'
    write_u32be(out, 0);           // total length patched below

    for (const IcnsSlot& slot : kIcnsSlots) {
        const std::vector<std::uint8_t> png = icon_encode_png(icon_resize_rgba(master, slot.size));
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(slot.tag[i]));
        }
        write_u32be(out, static_cast<std::uint32_t>(8 + png.size())); // chunk length includes header
        out.insert(out.end(), png.begin(), png.end());
    }

    const std::uint32_t total = static_cast<std::uint32_t>(out.size());
    out[4] = static_cast<std::uint8_t>((total >> 24) & 0xFFu);
    out[5] = static_cast<std::uint8_t>((total >> 16) & 0xFFu);
    out[6] = static_cast<std::uint8_t>((total >> 8) & 0xFFu);
    out[7] = static_cast<std::uint8_t>(total & 0xFFu);
    return out;
}

std::expected<void, IconCodegenError> icon_codegen_write(
        std::string_view input_png_path, std::string_view output_dir) {
    const std::filesystem::path input_path(input_png_path);
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        return std::unexpected(
                IconCodegenError{IconCodegenErrorKind::Io, "failed to read " + input_path.generic_string()});
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const std::optional<render::TextureDesc> decoded = decode_png_rgba(bytes);
    if (!decoded) {
        return std::unexpected(
                IconCodegenError{IconCodegenErrorKind::Decode, "not a valid PNG: " + input_path.generic_string()});
    }
    if (decoded->width != decoded->height) {
        return std::unexpected(IconCodegenError{IconCodegenErrorKind::NotSquare,
                "icon master must be square, got " + std::to_string(decoded->width) + "x" +
                        std::to_string(decoded->height)});
    }
    if (decoded->width < kIconCodegenMinSize) {
        return std::unexpected(IconCodegenError{IconCodegenErrorKind::TooSmall,
                "icon master must be at least " + std::to_string(kIconCodegenMinSize) + "x" +
                        std::to_string(kIconCodegenMinSize) + ", got " + std::to_string(decoded->width) + "x" +
                        std::to_string(decoded->height)});
    }

    const std::filesystem::path out_root(output_dir);
    std::error_code create_ec;
    std::filesystem::create_directories(out_root, create_ec);
    if (create_ec) {
        return std::unexpected(
                IconCodegenError{IconCodegenErrorKind::Io, "failed to create " + out_root.generic_string()});
    }

    if (auto r = write_binary_file(out_root / "icon.ico", icon_encode_ico(*decoded, kIcoSizes)); !r) {
        return r;
    }
    if (auto r = write_binary_file(out_root / "icon.icns", icon_encode_icns(*decoded)); !r) {
        return r;
    }
    for (const AndroidMipmap& mipmap : kAndroidMipmaps) {
        const std::filesystem::path rel = std::filesystem::path(mipmap.dir) / "ic_launcher.png";
        if (auto r = write_binary_file(out_root / rel, icon_encode_png(icon_resize_rgba(*decoded, mipmap.size))); !r) {
            return r;
        }
    }
    if (auto r = write_binary_file(out_root / "favicon.png", icon_encode_png(icon_resize_rgba(*decoded, kFaviconSize)));
            !r) {
        return r;
    }

    return {};
}

}
