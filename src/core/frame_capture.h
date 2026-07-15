#pragma once

#include <engine/core/window_desc.h>
#include <engine/render/graphic_factory.h>

namespace engine {

// One window's pixels as this frame drew them, read after drawing and before its swap. GameLoop names the window;
// IPresentation::draw_all fills `image`.
struct FrameCapture {
    WindowId window = kPrimaryWindow;
    // RGBA8, top row first, in drawable pixels. Empty when the window did not draw this frame.
    render::TextureDesc image;
};

}
