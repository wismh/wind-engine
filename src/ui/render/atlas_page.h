#pragma once

#include "ui/render/skyline_packer.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::ui {

// A pixel rectangle [x0, x1) x [y0, y1) of an atlas page.
struct AtlasRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    [[nodiscard]] bool empty() const noexcept { return x0 >= x1 || y0 >= y1; }
    friend bool operator==(const AtlasRect&, const AtlasRect&) = default;
};

// One page of an atlas on the CPU (R8 for coverage, RGBA8 for images and gradient ramps): its pixels, its packer, and
// the record a GPU device needs to catch up. Each
// write stamps the page with the owning atlas's next version; a device remembers the version it last uploaded and
// sends only `dirty_since(that)`. Several devices (one per window) each keep their own number, so no one clears a
// shared dirty flag under another. A device that has no texture for the page yet uploads all of it.
class AtlasPage {
public:
    // `channels` is 1 (R8) or 4 (RGBA8, straight or premultiplied as the owning atlas documents).
    AtlasPage(int width, int height, int channels = 1);

    [[nodiscard]] int width() const noexcept { return packer_.width(); }
    [[nodiscard]] int height() const noexcept { return packer_.height(); }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    // Row-major, `width() * channels()` bytes per row.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return pixels_; }
    // The atlas version of the last write to this page.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    // Union of every rect written after `version`; empty when nothing was.
    [[nodiscard]] AtlasRect dirty_since(std::uint64_t version) const;

    SkylinePacker& packer() noexcept { return packer_; }
    std::uint8_t* row(int y) noexcept { return pixels_.data() + static_cast<std::size_t>(y) * pixels_width(); }
    // Records that `rect` changed at atlas version `version` (increasing from call to call).
    void mark_dirty(const AtlasRect& rect, std::uint64_t version);
    // Forgets every packed rectangle so the page can be packed again from empty (an evicted cache page). The pixels
    // stay until they are overwritten; whoever packs into the page writes every texel of the cells it takes.
    void reset_packing();

private:
    struct Write {
        std::uint64_t version = 0;
        AtlasRect rect;
    };

    [[nodiscard]] std::size_t pixels_width() const noexcept {
        return static_cast<std::size_t>(packer_.width()) * static_cast<std::size_t>(channels_);
    }

    SkylinePacker packer_;
    int channels_ = 1;
    std::vector<std::uint8_t> pixels_;
    std::uint64_t version_ = 0;
    std::vector<Write> writes_;
};

}
