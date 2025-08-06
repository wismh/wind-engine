#pragma once

// docs/tech/features/CLI.md

#include "cli/cli_server.h"

#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/render/graphic_factory.h>

#include <optional>
#include <string>

namespace engine::cli {

    // `rect` grown outward to whole pixels and clipped to a `width` x `height` image. `w` or `h` is 0 when nothing of
    // it is inside.
    [[nodiscard]] render::Rect snap_to_pixels(const render::Rect &rect, int width, int height);

    // The pixels of `image` inside `box`, a snap_to_pixels result for that image.
    [[nodiscard]] render::TextureDesc crop_image(const render::TextureDesc &image, const render::Rect &box);

    // The body a `screenshot` is refused with before any frame is read: `path` is empty or not absolute.
    [[nodiscard]] std::optional<std::string> screenshot_request_error(const CliRequest &request);

    // `screenshot`: `image` (the request's window as this frame drew it) cropped to the selected element's border box
    // (searched only on `request.canvas` when set), to that canvas's rect when there is a canvas and no selector, or
    // whole, written as a PNG to `request.path`. `world` is that window's world, null when it has none.
    [[nodiscard]] std::string screenshot_json(ecs::World *world, const CliRequest &request,
                                              const render::TextureDesc &image);

} // namespace engine::cli
