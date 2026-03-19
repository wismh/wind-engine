#include <gtest/gtest.h>

#include "ui/render/paint_resources.h"
#include "ui/render/ui_device.h"
#include "ui/render/ui_renderer.h"

#include <engine/builtin_ids.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/font.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

// wind-234 (5c): UiRenderer drives an IUiDevice. A fake device records the calls, so the texture catch-up of each
// window's device and the frame's draws are checked without a GPU.

namespace {

using engine::AssetId;
using engine::render::Rect;
using engine::ui::AtlasRect;
using engine::ui::PaintResources;
using engine::ui::UiBatch;
using engine::ui::UiIndex;
using engine::ui::UiRenderer;
using engine::ui::UiTextureFormat;
using engine::ui::UiTextureRegion;
using engine::ui::UiTextureSlot;
using engine::ui::UiVertex;

constexpr AssetId kImage{"d1a1c2d3e4f5678901234567890abc11"};
constexpr glm::vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};

const engine::Font& ui_font() {
    static const engine::Font font = [] {
        std::ifstream in(std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts/ui.ttf", std::ios::binary);
        engine::Font f;
        f.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return f;
    }();
    return font;
}

engine::render::TextureDesc image(int w, int h) {
    engine::render::TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, std::uint8_t{200});
    return desc;
}

enum class Call { BeginFrame, CreateTexture, UpdateTexture, ReleaseTextures, Upload, Draw, EndFrame };

struct Record {
    Call call = Call::BeginFrame;
    UiTextureSlot slot = UiTextureSlot::Glyph;
    std::uint32_t page = 0;
    UiTextureFormat format = UiTextureFormat::R8;
    AtlasRect rect;
    std::size_t count = 0;
};

class RecordingDevice final : public engine::ui::IUiDevice {
public:
    std::vector<Record> calls;
    glm::vec2 size{};
    float ratio = 0.0f;
    std::size_t vertices = 0;
    std::size_t indices = 0;
    std::vector<UiBatch> batches;
    int draws = 0;

    void begin_frame(float width, float height, float pixel_ratio) override {
        calls.push_back({.call = Call::BeginFrame});
        size = {width, height};
        ratio = pixel_ratio;
        draws = 0;
    }
    void create_texture(UiTextureSlot slot, std::uint32_t page, int width, int height, UiTextureFormat format,
            const UiTextureRegion& texels) override {
        EXPECT_EQ(texels.rect, (AtlasRect{0, 0, width, height}));
        EXPECT_EQ(texels.row_texels, width);
        EXPECT_EQ(texels.pixels.size(),
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                        (format == UiTextureFormat::R8 ? 1u : 4u));
        calls.push_back({.call = Call::CreateTexture, .slot = slot, .page = page, .format = format,
                .rect = texels.rect});
    }
    void update_texture(UiTextureSlot slot, std::uint32_t page, const UiTextureRegion& texels) override {
        calls.push_back({.call = Call::UpdateTexture, .slot = slot, .page = page, .rect = texels.rect});
    }
    void release_textures() override { calls.push_back({.call = Call::ReleaseTextures}); }
    void upload(std::span<const UiVertex> v, std::span<const UiIndex> i) override {
        calls.push_back({.call = Call::Upload});
        vertices = v.size();
        indices = i.size();
    }
    void draw(std::span<const UiBatch> b) override {
        calls.push_back({.call = Call::Draw, .count = b.size()});
        batches.assign(b.begin(), b.end());
        for (const UiBatch& batch : b) {
            draws += batch.index_count > 0 ? 1 : 0;
        }
    }
    void end_frame() override { calls.push_back({.call = Call::EndFrame}); }
    [[nodiscard]] int draw_calls() const override { return draws; }

    [[nodiscard]] std::vector<Record> of(Call call) const {
        std::vector<Record> out;
        for (const Record& r : calls) {
            if (r.call == call) {
                out.push_back(r);
            }
        }
        return out;
    }
};

void paint_text_and_image(UiRenderer& renderer, std::string_view text) {
    renderer.begin_frame(640.0f, 480.0f, 1.0f);
    renderer.fill_rounded_rect(Rect{10.0f, 10.0f, 100.0f, 40.0f}, 6.0f, kWhite);
    renderer.fill_text(text, {20.0f, 30.0f}, kWhite, engine::ui::UiAlign::Start, engine::ui::UiAlign::Center);
    renderer.image(kImage, Rect{200.0f, 10.0f, 32.0f, 32.0f});
}

struct Fixture {
    PaintResources resources;
    RecordingDevice device;
    UiRenderer renderer{resources, device};

    Fixture() {
        EXPECT_TRUE(renderer.load_ui_font(ui_font()));
        EXPECT_TRUE(renderer.add_image(kImage, image(16, 16)));
    }
};

}

TEST(UiRenderer, FirstFrameCreatesEveryPageThenDrawsTheList) {
    Fixture f;
    paint_text_and_image(f.renderer, "Hello");
    const int queued = f.renderer.queued_draw_calls();
    f.renderer.end_frame();

    ASSERT_GE(f.device.calls.size(), 5u);
    EXPECT_EQ(f.device.calls.front().call, Call::BeginFrame);
    EXPECT_EQ(f.device.calls.back().call, Call::EndFrame);
    EXPECT_EQ(f.device.size, glm::vec2(640.0f, 480.0f));
    EXPECT_EQ(f.device.ratio, 1.0f);

    const std::vector<Record> created = f.device.of(Call::CreateTexture);
    ASSERT_EQ(created.size(), 2u);
    EXPECT_EQ(created[0].slot, UiTextureSlot::Glyph);
    EXPECT_EQ(created[0].format, UiTextureFormat::R8);
    EXPECT_EQ(created[1].slot, UiTextureSlot::Image);
    EXPECT_EQ(created[1].format, UiTextureFormat::Rgba8);
    EXPECT_TRUE(f.device.of(Call::UpdateTexture).empty());

    // Textures before geometry, geometry before the draw.
    const auto at = [&](Call call) {
        for (std::size_t i = 0; i < f.device.calls.size(); ++i) {
            if (f.device.calls[i].call == call) {
                return i;
            }
        }
        return f.device.calls.size();
    };
    EXPECT_LT(at(Call::CreateTexture), at(Call::Upload));
    EXPECT_LT(at(Call::Upload), at(Call::Draw));

    EXPECT_EQ(f.device.vertices, f.renderer.list().vertices().size());
    EXPECT_EQ(f.device.indices, f.renderer.list().indices().size());
    EXPECT_EQ(f.device.batches.size(), f.renderer.list().batches().size());
    // The profiler's draw_calls is the batch count, and it is what the device issues.
    EXPECT_EQ(queued, static_cast<int>(f.renderer.list().batches().size()));
    EXPECT_EQ(f.device.draw_calls(), queued);
    EXPECT_EQ(queued, 1);
}

TEST(UiRenderer, UnchangedPagesAreNotUploadedAgain) {
    Fixture f;
    paint_text_and_image(f.renderer, "Hello");
    f.renderer.end_frame();
    f.device.calls.clear();

    paint_text_and_image(f.renderer, "Hello");
    f.renderer.end_frame();
    EXPECT_TRUE(f.device.of(Call::CreateTexture).empty());
    EXPECT_TRUE(f.device.of(Call::UpdateTexture).empty());
    EXPECT_EQ(f.device.of(Call::Draw).size(), 1u);
}

TEST(UiRenderer, NewGlyphsUpdateOnlyTheirRegion) {
    Fixture f;
    paint_text_and_image(f.renderer, "Hello");
    f.renderer.end_frame();
    const engine::ui::AtlasPage& page = f.resources.fonts.atlas().page(0);
    const std::uint64_t seen = page.version();
    f.device.calls.clear();

    paint_text_and_image(f.renderer, "Hello, wxyz");
    f.renderer.end_frame();
    EXPECT_TRUE(f.device.of(Call::CreateTexture).empty());
    const std::vector<Record> updated = f.device.of(Call::UpdateTexture);
    ASSERT_EQ(updated.size(), 1u);
    EXPECT_EQ(updated[0].slot, UiTextureSlot::Glyph);
    EXPECT_EQ(updated[0].page, 0u);
    EXPECT_EQ(updated[0].rect, page.dirty_since(seen));
    EXPECT_FALSE(updated[0].rect.empty());
    EXPECT_LT(updated[0].rect.x1 - updated[0].rect.x0, page.width());
}

TEST(UiRenderer, EveryWindowsDeviceCatchesUpOnItsOwn) {
    PaintResources shared;
    RecordingDevice first_device;
    RecordingDevice second_device;
    UiRenderer first{shared, first_device};
    UiRenderer second{shared, second_device};
    // Both windows register the same font and image: one face, one image in the shared atlases.
    ASSERT_TRUE(first.load_ui_font(ui_font()));
    ASSERT_TRUE(second.load_ui_font(ui_font()));
    ASSERT_TRUE(first.add_image(kImage, image(16, 16)));
    ASSERT_TRUE(second.add_image(kImage, image(16, 16)));
    EXPECT_EQ(shared.images.image_count(), 1u);

    paint_text_and_image(first, "Hello");
    first.end_frame();
    EXPECT_EQ(first_device.of(Call::CreateTexture).size(), 2u);

    // The second window draws new glyphs: its device creates every page whole, the first device has not seen them.
    paint_text_and_image(second, "Other words");
    second.end_frame();
    EXPECT_EQ(second_device.of(Call::CreateTexture).size(), 2u);
    EXPECT_TRUE(second_device.of(Call::UpdateTexture).empty());

    // Back in the first window, only what the second window added since the first device's upload.
    first_device.calls.clear();
    paint_text_and_image(first, "Hello");
    first.end_frame();
    EXPECT_TRUE(first_device.of(Call::CreateTexture).empty());
    const std::vector<Record> updated = first_device.of(Call::UpdateTexture);
    ASSERT_EQ(updated.size(), 1u);
    EXPECT_EQ(updated[0].slot, UiTextureSlot::Glyph);
}

TEST(UiRenderer, DroppedImagesReleaseTexturesAndCreateThemAgain) {
    Fixture f;
    paint_text_and_image(f.renderer, "Hello");
    f.renderer.end_frame();
    f.device.calls.clear();

    f.renderer.drop_images();
    EXPECT_EQ(f.resources.images.image_count(), 0u);
    EXPECT_TRUE(f.resources.fonts.has_font(engine::builtin::font_ui));
    ASSERT_TRUE(f.renderer.add_image(kImage, image(8, 8)));

    paint_text_and_image(f.renderer, "Hello");
    f.renderer.end_frame();
    ASSERT_GE(f.device.calls.size(), 2u);
    EXPECT_EQ(f.device.calls[1].call, Call::ReleaseTextures);
    const std::vector<Record> created = f.device.of(Call::CreateTexture);
    ASSERT_EQ(created.size(), 2u);
    EXPECT_EQ(created[0].slot, UiTextureSlot::Glyph);
    EXPECT_EQ(created[1].slot, UiTextureSlot::Image);
}

TEST(UiRenderer, AnEmptyFrameMakesNoDeviceCall) {
    Fixture f;
    f.renderer.begin_frame(640.0f, 480.0f, 2.0f);
    f.renderer.end_frame();
    EXPECT_TRUE(f.device.calls.empty());
    EXPECT_EQ(f.renderer.queued_draw_calls(), 0);
}

TEST(UiRenderer, PixelRatioReachesTheDevice) {
    Fixture f;
    f.renderer.begin_frame(320.0f, 240.0f, 2.0f);
    f.renderer.fill_rounded_rect(Rect{0.0f, 0.0f, 10.0f, 10.0f}, 0.0f, kWhite);
    f.renderer.end_frame();
    EXPECT_EQ(f.device.size, glm::vec2(320.0f, 240.0f));
    EXPECT_EQ(f.device.ratio, 2.0f);
    // An analytic fill samples nothing, yet the pages that exist (the image's; no glyph yet) still reach the device.
    const std::vector<Record> created = f.device.of(Call::CreateTexture);
    ASSERT_EQ(created.size(), 1u);
    EXPECT_EQ(created[0].slot, UiTextureSlot::Image);
}
