#include "format_select.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace engine::capture {
namespace {

std::int64_t pixels(const CaptureFormat& format) {
    return static_cast<std::int64_t>(format.width) * static_cast<std::int64_t>(format.height);
}

}

std::optional<CaptureFormat> pick_format(
        std::span<const CaptureFormat> offered, const std::optional<CaptureFormat>& wanted) {
    std::optional<CaptureFormat> best;
    for (const CaptureFormat& candidate : offered) {
        if (!best) {
            best = candidate;
            continue;
        }
        if (wanted) {
            const std::int64_t candidate_gap = std::llabs(pixels(candidate) - pixels(*wanted));
            const std::int64_t best_gap = std::llabs(pixels(*best) - pixels(*wanted));
            if (candidate_gap != best_gap) {
                if (candidate_gap < best_gap) {
                    best = candidate;
                }
                continue;
            }
            if (std::fabs(candidate.fps - wanted->fps) < std::fabs(best->fps - wanted->fps)) {
                best = candidate;
            }
            continue;
        }
        if (pixels(candidate) != pixels(*best)) {
            if (pixels(candidate) > pixels(*best)) {
                best = candidate;
            }
            continue;
        }
        if (candidate.fps > best->fps) {
            best = candidate;
        }
    }
    return best;
}

}
