#pragma once

#include "ui/render/atlas_page.h"

#include <engine/ui/document.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::ui {

// Where a gradient's ramp lives: a page and the v of its row's texel centers.
struct RampRow {
    std::uint32_t page = 0;
    float v = 0.0f;
};

// RGBA8 premultiplied pages of gradient ramps, kRampWidth texels (fragment.h) per row, one row per distinct ramp.
// A ramp is what the fragment stage samples at t in [0, 1] (texel i is t = i / (kRampWidth - 1)), built to keep
// the NanoVG painter's pixels:
//   - Linear and radial: NanoVG's nvgLinearGradient / nvgRadialGradient take the first and the last stop only and mix
//     them in premultiplied space; so do these rows, and two gradients that share those two colors share a row.
//   - Conic: that painter baked every stop (sample_gradient_stops, straight alpha) into a texture; the row holds the
//     same samples, premultiplied.
// Colors are compared at 1e-6 as its conic cache did. Rows are reused least recently used first once a
// page is full, never a row the current list drew from (a full page of those opens another page). CPU only; a device
// catches up with page versions. Main thread only.
class RampAtlas {
public:
    static constexpr int kRowsPerPage = 256;

    void begin_list() noexcept { ++serial_; }

    // The row for `gradient` (at least two stops; percents resolved, as parse_gradient leaves them).
    [[nodiscard]] RampRow row(const Gradient& gradient);

    [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
    [[nodiscard]] const AtlasPage& page(std::size_t index) const { return pages_[index]; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    // Rows holding a ramp.
    [[nodiscard]] std::size_t row_count() const noexcept { return lookup_.size(); }

private:
    struct Row {
        std::vector<std::int64_t> key;
        std::uint64_t hash = 0;
        std::uint64_t used = 0;
        bool filled = false;
    };

    void key_of(const Gradient& gradient);
    void write_row(std::size_t slot, const Gradient& gradient);
    [[nodiscard]] std::size_t take_slot();

    std::vector<AtlasPage> pages_;
    // Page-major: slot = page * kRowsPerPage + row.
    std::vector<Row> rows_;
    std::size_t next_free_ = 0;
    std::unordered_map<std::uint64_t, std::size_t> lookup_;
    std::vector<std::int64_t> key_;
    std::uint64_t serial_ = 1;
    std::uint64_t version_ = 0;
};

}
