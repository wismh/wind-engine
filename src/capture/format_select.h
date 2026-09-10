#pragma once

// docs/tech/modules/Capture.md

#include <engine/capture/capture_system.h>

#include <optional>
#include <span>

namespace engine::capture {

// The offered format closest to `wanted`: smallest pixel-count difference, then smallest frame-rate
// difference, then the earlier entry. With no `wanted`, the largest picture, then the fastest. `nullopt` when
// nothing is offered.
[[nodiscard]] std::optional<CaptureFormat> pick_format(
        std::span<const CaptureFormat> offered, const std::optional<CaptureFormat>& wanted);

}
