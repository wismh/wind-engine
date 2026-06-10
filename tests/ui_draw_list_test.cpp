#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/picture.h"
#include "ui/render/batching_painter.h"
#include "ui/render/fragment.h"
#include "ui/render/nine_slice.h"
#include "ui/render/paint_resources.h"
#include "ui/render/path_raster.h"
#include "ui/render/render_list.h"
#include "ui/render/ui_pipeline.h"
#include "ui/render/ui_vertex.h"

#include <engine/builtin_ids.h>
#include <engine/render/commands.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/font.h>
#include <engine/ui/document.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

// wind-234 (5b): the UI draw list. Painter calls become quads, and a batch ends only on a texture change. The
// fragment stage is checked through its CPU reference (fragment.h), so no GPU is involved.

namespace {

using engine::AssetId;
using engine::render::Rect;
using engine::ui::BatchingPainter;
using engine::ui::PaintResources;
using engine::ui::PathSegment;
using engine::ui::RenderList;
using engine::ui::UiBatch;
using engine::ui::UiFragment;
using engine::ui::UiShapeKind;
using engine::ui::UiTextureSlot;
using engine::ui::UiVertex;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr AssetId kImageA{"d1a1c2d3e4f5678901234567890abc01"};
constexpr AssetId kImageB{"d1a1c2d3e4f5678901234567890abc02"};

engine::Font read_builtin(const char* relative) {
    const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / relative;
    std::ifstream in(path, std::ios::binary);
    engine::Font font;
    font.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return font;
}

const engine::Font& ui_font() {
    static const engine::Font font = read_builtin("fonts/ui.ttf");
    return font;
}

// A w x h image whose texel (x, y) is (x, y, 7, 255).
engine::render::TextureDesc test_image(int w, int h) {
    engine::render::TextureDesc desc;
    desc.width = w;
    desc.height = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            desc.rgba.insert(desc.rgba.end(),
                    {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), std::uint8_t{7}, std::uint8_t{255}});
        }
    }
    return desc;
}

std::vector<PathSegment> square(glm::vec2 min, glm::vec2 max, bool clockwise = true) {
    const glm::vec2 a = min;
    const glm::vec2 b{max.x, min.y};
    const glm::vec2 c = max;
    const glm::vec2 d{min.x, max.y};
    if (clockwise) {
        return {{PathSegment::Kind::Move, a}, {PathSegment::Kind::Line, b}, {PathSegment::Kind::Line, c},
                {PathSegment::Kind::Line, d}};
    }
    return {{PathSegment::Kind::Move, a}, {PathSegment::Kind::Line, d}, {PathSegment::Kind::Line, c},
            {PathSegment::Kind::Line, b}};
}

struct Fixture {
    PaintResources resources;
    BatchingPainter painter{resources};

    explicit Fixture(int glyph_page = 1024, int mask_page = 1024, int image_page = 1024) :
        resources(glyph_page, mask_page, image_page) {
        EXPECT_TRUE(resources.fonts.add_font(engine::builtin::font_ui, ui_font()));
        painter.begin_frame(800.0f, 600.0f);
    }

    [[nodiscard]] const RenderList& list() const { return painter.list(); }
    [[nodiscard]] std::size_t quads() const { return list().quad_count(); }
    [[nodiscard]] const UiVertex& vertex(std::size_t quad, int corner) const {
        return list().vertices()[quad * 4 + static_cast<std::size_t>(corner)];
    }
};

// The interpolated fragment of quad `quad` at logical point `p` (quads are parallelograms, attributes affine).
UiFragment fragment_at(const RenderList& list, std::size_t quad, glm::vec2 p) {
    const UiVertex* v = list.vertices().data() + quad * 4;
    const glm::vec2 e1 = v[1].position - v[0].position;
    const glm::vec2 e2 = v[3].position - v[0].position;
    const glm::vec2 r = p - v[0].position;
    const float det = e1.x * e2.y - e1.y * e2.x;
    const float s = (r.x * e2.y - r.y * e2.x) / det;
    const float t = (e1.x * r.y - e1.y * r.x) / det;
    const UiFragment f0 = engine::ui::fragment_of(v[0]);
    const UiFragment f1 = engine::ui::fragment_of(v[1]);
    const UiFragment f3 = engine::ui::fragment_of(v[3]);
    UiFragment out = f0;
    out.uv = f0.uv + s * (f1.uv - f0.uv) + t * (f3.uv - f0.uv);
    out.local = f0.local + s * (f1.local - f0.local) + t * (f3.local - f0.local);
    out.shape = f0.shape + s * (f1.shape - f0.shape) + t * (f3.shape - f0.shape);
    out.clip = f0.clip + s * (f1.clip - f0.clip) + t * (f3.clip - f0.clip);
    out.aux = f0.aux + s * (f1.aux - f0.aux) + t * (f3.aux - f0.aux);
    return out;
}

float alpha_at(const RenderList& list, std::size_t quad, glm::vec2 p) {
    return engine::ui::shade_fragment(fragment_at(list, quad, p)).a;
}

glm::vec2 centroid(const Fixture& f, std::size_t quad) {
    glm::vec2 sum{};
    for (int i = 0; i < 4; ++i) {
        sum += f.vertex(quad, i).position;
    }
    return sum * 0.25f;
}

// A mixed run: shapes, text, a gradient, and a path, with clips, transforms, opacities, and colors varying.
void draw_mixed_scene(BatchingPainter& painter) {
    painter.set_font(engine::builtin::font_ui, 14.0f);
    painter.fill_rounded_rect({0, 0, 800, 600}, 0.0f, {0.1f, 0.1f, 0.1f, 1.0f});
    for (int i = 0; i < 6; ++i) {
        const auto fi = static_cast<float>(i);
        painter.save();
        painter.scissor({10.0f + fi * 20.0f, 10.0f, 300.0f, 200.0f});
        painter.set_opacity(0.3f + 0.1f * fi);
        painter.apply_transform({100.0f, 100.0f}, 0.1f * fi, 1.0f + 0.1f * fi);
        painter.fill_rounded_rect({20, 20, 120, 40}, 6.0f, {1.0f, 0.2f * fi, 0.0f, 1.0f});
        painter.stroke_rounded_rect({20, 20, 120, 40}, 6.0f, 2.0f, {0.0f, 1.0f, 0.0f, 1.0f});
        painter.fill_text("Score 1234", {24, 30}, {1, 1, 1, 1}, engine::ui::UiAlign::Start,
                engine::ui::UiAlign::Start);
        painter.draw_line({20, 70}, {140, 70}, {1, 1, 0, 1}, 1.5f);
        painter.stroke_arc({60, 120}, 20.0f, 0.0f, kPi, 3.0f, {0, 0.5f, 1, 1});
        painter.restore();
    }
    engine::ui::Gradient gradient;
    gradient.kind = engine::ui::GradientKind::Linear;
    gradient.angle_deg = 90.0f;
    gradient.stops = {{{1, 0, 0, 1}, 0.0f}, {{0, 0, 1, 1}, 100.0f}};
    painter.fill_rounded_rect_gradient({400, 20, 100, 30}, 4.0f, gradient);
    const auto path = square({500, 100}, {520, 130});
    painter.fill_path(path, {1, 1, 1, 1});
}

TEST(UiDrawList, ShapesTextAndArcsUnderVaryingStateAreOneBatch) {
    Fixture f;
    draw_mixed_scene(f.painter);
    f.painter.end_frame();
    ASSERT_EQ(f.list().batches().size(), 1u) << "clip, transform, opacity, color, and kind never split a batch";
    EXPECT_EQ(f.painter.queued_draw_calls(), 1);
    const UiBatch& batch = f.list().batches()[0];
    EXPECT_EQ(batch.pages[static_cast<std::size_t>(UiTextureSlot::Glyph)], 0u);
    EXPECT_EQ(batch.pages[static_cast<std::size_t>(UiTextureSlot::Ramp)], 0u);
    EXPECT_EQ(batch.pages[static_cast<std::size_t>(UiTextureSlot::Mask)], 0u);
    EXPECT_EQ(batch.pages[static_cast<std::size_t>(UiTextureSlot::Image)], UiBatch::kUnbound);
    EXPECT_EQ(batch.index_offset, 0u);
    EXPECT_EQ(batch.index_count, f.list().indices().size());
    EXPECT_EQ(f.list().indices().size(), f.quads() * 6);
}

TEST(UiDrawList, RenderListBreaksOnlyWhenABoundSlotChangesPage) {
    RenderList list;
    static_cast<void>(list.push_quad());
    list.bind(UiTextureSlot::Glyph, 0);
    static_cast<void>(list.push_quad());
    list.bind(UiTextureSlot::Image, 3);
    list.bind(UiTextureSlot::Glyph, 0);
    static_cast<void>(list.push_quad());
    EXPECT_EQ(list.batches().size(), 1u) << "a slot not yet used is bound in place";
    list.bind(UiTextureSlot::Image, 4);
    static_cast<void>(list.push_quad());
    ASSERT_EQ(list.batches().size(), 2u);
    EXPECT_EQ(list.batches()[1].index_offset, 18u);
    EXPECT_EQ(list.batches()[1].pages[static_cast<std::size_t>(UiTextureSlot::Glyph)], UiBatch::kUnbound)
            << "a new batch binds only what it needs";
    list.bind(UiTextureSlot::Glyph, 1);
    EXPECT_EQ(list.batches().size(), 2u);
    list.bind(UiTextureSlot::Glyph, 2);
    ASSERT_EQ(list.batches().size(), 3u);
    list.bind(UiTextureSlot::Glyph, 5);
    EXPECT_EQ(list.batches().size(), 3u) << "a batch with no quads yet is rebound in place";
    EXPECT_EQ(list.batches()[2].pages[static_cast<std::size_t>(UiTextureSlot::Glyph)], 5u);
    list.clear();
    EXPECT_TRUE(list.batches().empty());
}

TEST(UiDrawList, AGlyphPageChangeStartsABatch) {
    Fixture f(64);
    f.painter.set_font(engine::builtin::font_ui, 40.0f);
    f.painter.fill_text("ABCDEFGHIJKLMNOP", {10, 10}, {1, 1, 1, 1}, engine::ui::UiAlign::Start,
            engine::ui::UiAlign::Start);
    ASSERT_GT(f.resources.fonts.atlas().page_count(), 1u);
    const auto& batches = f.list().batches();
    ASSERT_GT(batches.size(), 1u);
    for (std::size_t i = 1; i < batches.size(); ++i) {
        EXPECT_NE(batches[i].pages[0], batches[i - 1].pages[0]);
    }
    std::uint32_t indices = 0;
    for (const UiBatch& batch : batches) {
        EXPECT_NE(batch.pages[0], UiBatch::kUnbound);
        EXPECT_EQ(batch.index_offset, indices);
        indices += batch.index_count;
    }
    EXPECT_EQ(indices, f.list().indices().size());
}

TEST(UiDrawList, AnImagePageChangeStartsABatchAndSlotsMixFreely) {
    Fixture f(1024, 1024, 32);
    ASSERT_TRUE(f.resources.images.add(kImageA, test_image(20, 20)));
    ASSERT_TRUE(f.resources.images.add(kImageB, test_image(20, 20)));
    ASSERT_EQ(f.resources.images.page_count(), 2u) << "two 22 px cells do not share a 32 px page";
    f.painter.image(kImageA, {0, 0, 20, 20});
    f.painter.fill_text("text", {0, 30}, {1, 1, 1, 1}, engine::ui::UiAlign::Start, engine::ui::UiAlign::Start);
    f.painter.fill_path(square({0, 50}, {10, 60}), {1, 1, 1, 1});
    f.painter.image(kImageA, {30, 0, 20, 20});
    EXPECT_EQ(f.list().batches().size(), 1u) << "glyph, mask, and image pages bind side by side";
    f.painter.image(kImageB, {60, 0, 20, 20});
    f.painter.image(kImageA, {90, 0, 20, 20});
    EXPECT_EQ(f.list().batches().size(), 3u);

    Fixture shared(1024, 1024, 128);
    ASSERT_TRUE(shared.resources.images.add(kImageA, test_image(20, 20)));
    ASSERT_TRUE(shared.resources.images.add(kImageB, test_image(20, 20)));
    shared.painter.image(kImageA, {0, 0, 20, 20});
    shared.painter.image(kImageB, {60, 0, 20, 20});
    shared.painter.image(kImageA, {90, 0, 20, 20});
    EXPECT_EQ(shared.list().batches().size(), 1u) << "images on one page share a batch";
}

TEST(UiDrawList, AMaskPageChangeStartsABatch) {
    Fixture f(1024, 64);
    f.painter.fill_path(square({0, 0}, {50, 50}), {1, 1, 1, 1});
    f.painter.fill_path(square({0, 0}, {50, 40}), {1, 1, 1, 1});
    EXPECT_EQ(f.resources.masks.page_count(), 2u);
    f.painter.fill_path(square({0, 0}, {50, 50}), {1, 1, 1, 1});
    EXPECT_EQ(f.list().batches().size(), 3u);
    EXPECT_EQ(f.resources.masks.rasterized_count(), 2u);
}

TEST(UiDrawList, VertexPositionsFollowTheTransformAndTheView) {
    Fixture f;
    f.painter.save();
    f.painter.apply_transform({60, 45}, 0.5f, 2.0f);
    f.painter.fill_rounded_rect({10, 20, 100, 50}, 0.0f, {1, 1, 1, 1});
    f.painter.restore();
    ASSERT_EQ(f.quads(), 1u);
    const glm::vec2 c = centroid(f, 0);
    EXPECT_NEAR(c.x, 60.0f, 1e-3f);
    EXPECT_NEAR(c.y, 45.0f, 1e-3f);
    // The rect's corner, wherever the transform put it, is on the edge: half covered (a sharp corner).
    const glm::vec2 offset = glm::vec2(10.0f, 20.0f) - glm::vec2(60.0f, 45.0f);
    const float cs = std::cos(0.5f);
    const float sn = std::sin(0.5f);
    const glm::vec2 turned{cs * offset.x - sn * offset.y, sn * offset.x + cs * offset.y};
    const glm::vec2 corner = glm::vec2(60, 45) + 2.0f * turned;
    EXPECT_NEAR(alpha_at(f.list(), 0, corner), 0.5f, 0.02f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 45}), 1.0f, 1e-4f);
    // The quad reaches one device pixel past the shape for the antialiasing ramp: local px at the corner vertex.
    EXPECT_NEAR(f.vertex(0, 0).local.x, -(50.0f * 2.0f + 1.0f), 1e-3f);
    EXPECT_NEAR(f.vertex(0, 0).shape.x, 100.0f, 1e-3f);

    f.painter.save();
    f.painter.apply_view({100, 100}, {10, 0}, 2.0f);
    f.painter.fill_rounded_rect({0, 0, 20, 20}, 0.0f, {1, 1, 1, 1});
    f.painter.restore();
    const glm::vec2 v = centroid(f, 1);
    EXPECT_NEAR(v.x, 100.0f + 2.0f * (10.0f - 100.0f + 10.0f), 1e-3f);
    EXPECT_NEAR(v.y, 100.0f + 2.0f * (10.0f - 100.0f), 1e-3f);
}

TEST(UiDrawList, OpacityIsFoldedIntoThePremultipliedColor) {
    Fixture f;
    f.painter.set_opacity(0.5f);
    f.painter.fill_rounded_rect({0, 0, 10, 10}, 0.0f, {1.0f, 0.5f, 0.0f, 0.8f});
    const glm::vec4 c = engine::ui::unpack_color(f.vertex(0, 0).color);
    EXPECT_NEAR(c.a, 0.4f, 1.0f / 255.0f);
    EXPECT_NEAR(c.r, 0.4f, 1.0f / 255.0f);
    EXPECT_NEAR(c.g, 0.2f, 1.0f / 255.0f);
    EXPECT_EQ(c.b, 0.0f);

    // nvgGlobalAlpha sets, it does not multiply; restore brings the old value back.
    f.painter.save();
    f.painter.set_opacity(0.8f);
    f.painter.fill_rounded_rect({0, 0, 10, 10}, 0.0f, {1, 1, 1, 1});
    f.painter.restore();
    f.painter.fill_rounded_rect({0, 0, 10, 10}, 0.0f, {1, 1, 1, 1});
    EXPECT_EQ(f.vertex(1, 0).color >> 24, 204u);
    EXPECT_EQ(f.vertex(2, 0).color >> 24, 128u);

    // Images and gradients carry the opacity as a premultiplied white.
    ASSERT_TRUE(f.resources.images.add(kImageA, test_image(4, 4)));
    f.painter.image(kImageA, {0, 0, 4, 4});
    EXPECT_EQ(f.vertex(3, 0).color, 0x80808080u);
    EXPECT_EQ(f.list().batches().size(), 1u);
}

TEST(UiDrawList, AnAxisAlignedClipIsAPerVertexFrameAndDoesNotSplit) {
    Fixture f;
    f.painter.scissor({10, 10, 100, 50});
    f.painter.fill_rounded_rect({0, 0, 200, 200}, 0.0f, {1, 1, 1, 1});
    ASSERT_EQ(f.quads(), 1u);
    const glm::vec4 clip = f.vertex(0, 0).clip;
    EXPECT_NEAR(clip.x, -1.0f - 60.0f, 1e-4f);
    EXPECT_NEAR(clip.y, -1.0f - 35.0f, 1e-4f);
    EXPECT_FLOAT_EQ(clip.z, 50.0f);
    EXPECT_FLOAT_EQ(clip.w, 25.0f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 35}), 1.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {110, 35}), 0.5f, 1e-4f) << "the scissor ramp is centered on the edge";
    EXPECT_NEAR(alpha_at(f.list(), 0, {111, 35}), 0.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {110, 60}), 0.25f, 1e-4f);

    // A quad wholly outside the clip is dropped, and no empty batch is left behind.
    f.painter.fill_text("hidden", {300, 300}, {1, 1, 1, 1}, engine::ui::UiAlign::Start, engine::ui::UiAlign::Start);
    EXPECT_EQ(f.quads(), 1u);
    EXPECT_EQ(f.list().batches().size(), 1u);
    EXPECT_EQ(f.list().batches()[0].pages[0], UiBatch::kUnbound);

    f.painter.begin_frame(800, 600);
    f.painter.fill_rounded_rect({0, 0, 10, 10}, 0.0f, {1, 1, 1, 1});
    EXPECT_GE(f.vertex(0, 0).clip.z, 1.0e6f) << "no clip: an extent nothing reaches";
}

TEST(UiDrawList, ARotatedClipIsExactWithoutAStencil) {
    Fixture f;
    f.painter.apply_transform({50, 50}, kPi / 4.0f, 1.0f);
    f.painter.scissor({0, 0, 100, 100});
    f.painter.fill_rounded_rect({-100, -100, 300, 300}, 0.0f, {1, 1, 1, 1});
    ASSERT_EQ(f.quads(), 1u);
    // The clip is the square turned 45 degrees about its center: |dx| + |dy| <= 50 sqrt 2.
    EXPECT_NEAR(alpha_at(f.list(), 0, {50, 50}), 1.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {115, 50}), 1.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {95, 5}), 0.0f, 1e-4f) << "inside the bounding box, outside the clip";
    const float edge = 50.0f + 50.0f * std::sqrt(2.0f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {edge, 50}), 0.25f, 1e-3f) << "a corner of the turned clip";
    const glm::vec4 clip = fragment_at(f.list(), 0, {50 + 10, 50}).clip;
    EXPECT_NEAR(std::abs(clip.x), 10.0f / std::sqrt(2.0f), 1e-3f);
    EXPECT_NEAR(std::abs(clip.y), 10.0f / std::sqrt(2.0f), 1e-3f);
}

TEST(UiDrawList, NestedClipsIntersectOnTheOuterBox) {
    Fixture f;
    f.painter.scissor({0, 0, 100, 100});
    f.painter.scissor({50, 50, 100, 100});
    f.painter.fill_rounded_rect({0, 0, 200, 200}, 0.0f, {1, 1, 1, 1});
    EXPECT_FLOAT_EQ(f.vertex(0, 0).clip.z, 25.0f);
    EXPECT_FLOAT_EQ(f.vertex(0, 0).clip.w, 25.0f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {75, 75}), 1.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {40, 75}), 0.0f, 1e-4f);

    // Under a rotation the outer clip enters as its bounding box in the rotated frame (nvgIntersectScissor): a
    // 45 degree turn about the outer clip's center widens it to 50 sqrt 2 each side, the inner rect then cuts.
    Fixture r;
    r.painter.scissor({0, 0, 100, 100});
    r.painter.apply_transform({50, 50}, kPi / 4.0f, 1.0f);
    r.painter.scissor({0, -200, 100, 500});
    r.painter.fill_rounded_rect({-200, -200, 500, 500}, 0.0f, {1, 1, 1, 1});
    const float wide = 50.0f * std::sqrt(2.0f);
    EXPECT_NEAR(r.vertex(0, 0).clip.z, 50.0f, 1e-3f);
    EXPECT_NEAR(r.vertex(0, 0).clip.w, wide, 1e-3f);
    EXPECT_NEAR(alpha_at(r.list(), 0, {115, 50}), 1.0f, 1e-4f) << "the approximation kept for its pixels";
}

TEST(UiDrawList, NineSliceDrawsNineQuadsWithTheirImageCoordinates) {
    Fixture f;
    ASSERT_TRUE(f.resources.images.add(kImageA, test_image(30, 30)));
    f.painter.image_nine_slice(kImageA, {0, 0, 90, 60}, engine::ui::BoxInsets{10, 10, 10, 10});
    ASSERT_EQ(f.quads(), 9u);
    EXPECT_EQ(f.list().batches().size(), 1u);
    const auto pieces = engine::ui::nine_slice_pieces({0, 0, 90, 60}, {10, 10, 10, 10}, {30, 30});
    ASSERT_EQ(pieces.count, 9u);
    // Top left piece: its outer corner is image (0, 0), its inner corner (1/3, 1/3).
    EXPECT_NEAR(fragment_at(f.list(), 0, {0, 0}).uv.x, 0.0f, 1e-5f);
    EXPECT_NEAR(fragment_at(f.list(), 0, {10, 10}).uv.y, 1.0f / 3.0f, 1e-5f);
    // Center piece: 70 x 40 px showing the image's middle third.
    const UiFragment center_tl = fragment_at(f.list(), 4, {10, 10});
    const UiFragment center_br = fragment_at(f.list(), 4, {80, 50});
    EXPECT_NEAR(center_tl.uv.x, 1.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(center_br.uv.x, 2.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(center_br.uv.y, 2.0f / 3.0f, 1e-5f);
    // Bottom right piece ends at image (1, 1).
    const UiFragment br = fragment_at(f.list(), 8, {90, 60});
    EXPECT_NEAR(br.uv.x, 1.0f, 1e-5f);
    EXPECT_NEAR(br.uv.y, 1.0f, 1e-5f);
    // The fetch maps image coordinates into the image's texel rect on its page.
    const engine::ui::AtlasImage* img = f.resources.images.find(kImageA);
    ASSERT_NE(img, nullptr);
    const auto fetch = engine::ui::fragment_fetch(center_br);
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->slot, UiTextureSlot::Image);
    EXPECT_NEAR(fetch->uv.x, img->uv_min.x + img->uv_size.x * 2.0f / 3.0f, 1e-6f);
}

TEST(UiDrawList, ImageRepeatWrapsInsideTheImageRect) {
    Fixture f;
    ASSERT_TRUE(f.resources.images.add(kImageA, test_image(16, 8)));
    f.painter.image_repeat(kImageA, {0, 0, 64, 24});
    ASSERT_EQ(f.quads(), 1u);
    const UiFragment far = fragment_at(f.list(), 0, {64, 24});
    EXPECT_NEAR(far.uv.x, 4.0f, 1e-4f);
    EXPECT_NEAR(far.uv.y, 3.0f, 1e-4f);
    const UiFragment inside = fragment_at(f.list(), 0, {20, 4});
    const auto fetch = engine::ui::fragment_fetch(inside);
    ASSERT_TRUE(fetch.has_value());
    const engine::ui::AtlasImage* img = f.resources.images.find(kImageA);
    EXPECT_NEAR(fetch->uv.x, img->uv_min.x + 0.25f * img->uv_size.x, 1e-5f);
    EXPECT_NEAR(fetch->uv.y, img->uv_min.y + 0.5f * img->uv_size.y, 1e-5f);

    // The gutter holds the opposite edge, as repeat sampling reads it.
    const engine::ui::AtlasPage& page = f.resources.images.page(img->page);
    const int x0 = static_cast<int>(std::lround(img->uv_min.x * static_cast<float>(page.width())));
    const int y0 = static_cast<int>(std::lround(img->uv_min.y * static_cast<float>(page.height())));
    const auto texel = [&](int x, int y) {
        return page.pixels()[(static_cast<std::size_t>(y) * page.width() + static_cast<std::size_t>(x)) * 4];
    };
    EXPECT_EQ(texel(x0 - 1, y0), 15) << "left gutter = last column";
    EXPECT_EQ(texel(x0 + 16, y0), 0) << "right gutter = first column";
    EXPECT_EQ(texel(x0 + 3, y0), 3);
}

TEST(UiDrawList, PathMasksAreCachedByContentScaleAndPhase) {
    Fixture f;
    const auto path = square({10, 10}, {30, 25});
    f.painter.fill_path(path, {1, 1, 1, 1});
    f.painter.fill_path(path, {1, 0, 0, 1});
    EXPECT_EQ(f.resources.masks.rasterized_count(), 1u);
    const std::uint64_t version = f.resources.masks.version();

    f.painter.begin_frame(800, 600);
    f.painter.save();
    f.painter.apply_transform({0, 0}, 0.0f, 1.0f);
    f.painter.apply_view({0, 0}, {7, 3}, 1.0f);
    f.painter.fill_path(path, {1, 1, 1, 1});
    f.painter.restore();
    EXPECT_EQ(f.resources.masks.rasterized_count(), 1u) << "a whole-pixel move redraws the cached mask";
    EXPECT_EQ(f.resources.masks.version(), version);
    // The quad moved with it: the cell's interior starts one texel in, at the padded origin.
    const glm::vec2 moved = f.vertex(0, 0).position;
    f.painter.fill_path(path, {1, 1, 1, 1});
    EXPECT_NEAR(moved.x - f.vertex(1, 0).position.x, 7.0f, 1e-4f);
    EXPECT_NEAR(moved.y - f.vertex(1, 0).position.y, 3.0f, 1e-4f);

    f.painter.save();
    f.painter.apply_view({0, 0}, {0.5f, 0}, 1.0f);
    f.painter.fill_path(path, {1, 1, 1, 1});
    f.painter.restore();
    EXPECT_EQ(f.resources.masks.rasterized_count(), 2u) << "another phase is another mask";
    f.painter.save();
    f.painter.apply_transform({0, 0}, 0.0f, 2.0f);
    f.painter.fill_path(path, {1, 1, 1, 1});
    f.painter.restore();
    EXPECT_EQ(f.resources.masks.rasterized_count(), 3u) << "another scale is another mask";
    EXPECT_EQ(f.list().batches().size(), 1u);
    EXPECT_EQ(f.resources.masks.mask_count(), 3u);
}

TEST(UiDrawList, PathCoverageKeepsHolesAndUnionsSolids) {
    engine::ui::PathRasterizer raster;
    std::vector<std::uint8_t> pixels(32 * 32);
    const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 32 + x]; };

    ASSERT_TRUE(raster.flatten(square({2, 2}, {12, 12}), {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(5, 5), 255);
    EXPECT_EQ(at(11, 5), 255);
    EXPECT_EQ(at(12, 5), 0);
    EXPECT_EQ(at(1, 5), 0);

    ASSERT_TRUE(raster.flatten(square({2.5f, 2.5f}, {12.5f, 12.5f}), {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(2, 5), 128) << "a one-pixel ramp centered on the edge";
    EXPECT_EQ(at(2, 2), 128) << "the corner of a mitered fringe is half covered, not a quarter";
    ASSERT_TRUE(raster.flatten(square({2.25f, 2}, {12, 12}), {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(2, 5), 191) << "a center a quarter pixel inside";

    // A counter wound against the outline is a hole, whichever way the outline itself turns.
    for (const bool outer_clockwise : {true, false}) {
        std::vector<PathSegment> o = square({2, 2}, {22, 22}, outer_clockwise);
        const auto inner = square({8, 8}, {16, 16}, !outer_clockwise);
        o.insert(o.end(), inner.begin(), inner.end());
        ASSERT_TRUE(raster.flatten(o, {}));
        raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
        EXPECT_EQ(at(12, 12), 0);
        EXPECT_EQ(at(4, 12), 255);
    }
    // Same direction: a solid, and overlapping solids union instead of doubling.
    std::vector<PathSegment> both = square({2, 2}, {22, 22});
    const auto inner = square({8, 8}, {16, 16});
    both.insert(both.end(), inner.begin(), inner.end());
    ASSERT_TRUE(raster.flatten(both, {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(12, 12), 255);

    // A quadratic curve stays inside its control hull and covers its interior.
    const std::vector<PathSegment> bowl{{PathSegment::Kind::Move, {2, 2}},
            {PathSegment::Kind::Quad, {22, 2}, {12, 30}}, {PathSegment::Kind::Line, {2, 2}}};
    ASSERT_TRUE(raster.flatten(bowl, {}));
    EXPECT_LE(raster.bounds_max().y, 30.0f);
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(12, 6), 255);
    EXPECT_EQ(at(3, 20), 0);
}

TEST(UiDrawList, PathCoverageDrawsThinStemsAboutAPixelWide) {
    engine::ui::PathRasterizer raster;
    std::vector<std::uint8_t> pixels(32 * 32);
    const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 32 + x]; };

    // A stem 0.4 px wide: exact area would be 102. A convex fill culls the inverted inset and the two half
    // fringes (0.7 each at the center) blend to 0.91.
    ASSERT_TRUE(raster.flatten(square({10.3f, 2}, {10.7f, 20}), {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_NEAR(at(10, 10), 232, 1);
    EXPECT_EQ(at(9, 10), 0);
    EXPECT_EQ(at(11, 10), 0);

    // Two stems in one path are a stenciled fill: the inset is inverted, its winding is nonzero, the center is solid.
    std::vector<PathSegment> two = square({10.3f, 2}, {10.7f, 20});
    const auto other = square({20.3f, 2}, {20.7f, 20});
    two.insert(two.end(), other.begin(), other.end());
    ASSERT_TRUE(raster.flatten(two, {}));
    raster.rasterize({0, 0}, 32, 32, pixels.data(), 32);
    EXPECT_EQ(at(10, 10), 255);
    EXPECT_EQ(at(20, 10), 255);
    // Past the stem, the far edge's fringe still reaches (it starts 1.5 px inside its edge): 1.2 px in, alpha 0.3.
    EXPECT_NEAR(at(9, 10), 77, 1);
    EXPECT_EQ(at(8, 10), 0);
    EXPECT_LE(raster.bounds_min().x, 9.8f) << "bounds hold the fringe";
    EXPECT_GE(raster.bounds_max().x, 21.2f);
}

TEST(UiDrawList, StrokeCoversCapsJoinsAndCrossingsWithoutClosing) {
    engine::ui::PathRasterizer raster;
    std::vector<std::uint8_t> pixels(64 * 64);
    const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 64 + x]; };
    const auto draw = [&](const std::vector<PathSegment>& path, float width) {
        ASSERT_TRUE(raster.flatten_stroke(path, {}, width));
        raster.rasterize({0, 0}, 64, 64, pixels.data(), 64);
    };

    // Open segment, width 8. The end sits on a pixel center, so the cap is measured from there.
    draw({{PathSegment::Kind::Move, {10.5f, 20.5f}}, {PathSegment::Kind::Line, {50.5f, 20.5f}}}, 8.0f);
    EXPECT_EQ(at(50, 20), 255);
    EXPECT_EQ(at(52, 20), 255) << "a round cap stays solid for half the width past the end";
    EXPECT_NEAR(at(54, 20), 128, 1) << "the cap fringe is half covered one pixel further out";
    EXPECT_EQ(at(56, 20), 0);

    // An open corner does not grow a segment between its ends.
    draw({{PathSegment::Kind::Move, {8.5f, 8.5f}}, {PathSegment::Kind::Line, {30.5f, 8.5f}},
                 {PathSegment::Kind::Line, {30.5f, 30.5f}}},
            4.0f);
    EXPECT_EQ(at(20, 8), 255);
    EXPECT_EQ(at(19, 19), 0) << "the chord between the open ends is not stroked";

    std::vector<PathSegment> box = square({8.5f, 8.5f}, {40.5f, 40.5f});
    box.push_back({PathSegment::Kind::Close, {}});
    draw(box, 4.0f);
    EXPECT_EQ(at(24, 8), 255);
    EXPECT_EQ(at(24, 24), 0) << "a closed stroke leaves the middle empty";

    // A sharp tip. A miter would run well past the corner; a round join stays within the half width.
    draw({{PathSegment::Kind::Move, {8.5f, 20.5f}}, {PathSegment::Kind::Line, {50.5f, 16.5f}},
                 {PathSegment::Kind::Line, {8.5f, 12.5f}}},
            6.0f);
    EXPECT_EQ(at(29, 18), 255);
    EXPECT_EQ(at(58, 16), 0);

    draw({{PathSegment::Kind::Move, {5.5f, 20.5f}}, {PathSegment::Kind::Line, {45.5f, 20.5f}},
                 {PathSegment::Kind::Move, {20.5f, 5.5f}}, {PathSegment::Kind::Line, {20.5f, 45.5f}}},
            4.0f);
    EXPECT_EQ(at(10, 20), 255);
    EXPECT_EQ(at(20, 20), 255) << "a crossing is no darker than either arm";
    EXPECT_NEAR(at(22, 22), 128, 1) << "overlapping fringes take the maximum, not a sum";

    ASSERT_TRUE(raster.flatten(square({2, 2}, {22, 22}), {}));
    raster.rasterize({0, 0}, 64, 64, pixels.data(), 64);
    EXPECT_EQ(at(12, 12), 255) << "a fill still closes a contour that has no Close";
}

TEST(UiDrawList, ThinStrokeFadesAndWidthsDoNotShareAMask) {
    Fixture f;
    const std::vector<PathSegment> line{{PathSegment::Kind::Move, {10, 10}}, {PathSegment::Kind::Line, {40, 10}}};
    f.painter.stroke_path(line, 0.5f, {1, 1, 1, 1});
    ASSERT_EQ(f.quads(), 1u);
    EXPECT_NEAR(engine::ui::unpack_color(f.vertex(0, 0).color).a, 0.25f, 0.01f);
    EXPECT_EQ(f.resources.masks.rasterized_count(), 1u);

    f.painter.stroke_path(line, 0.25f, {1, 1, 1, 1});
    EXPECT_EQ(f.resources.masks.rasterized_count(), 1u) << "every sub-pixel width shares the one-pixel mask";

    f.painter.stroke_path(line, 4.0f, {1, 1, 1, 1});
    EXPECT_EQ(f.resources.masks.rasterized_count(), 2u);
    f.painter.stroke_path(line, 8.0f, {1, 1, 1, 1});
    EXPECT_EQ(f.resources.masks.rasterized_count(), 3u);
    f.painter.fill_path(square({10, 10}, {40, 30}), {1, 1, 1, 1});
    EXPECT_EQ(f.resources.masks.rasterized_count(), 4u) << "a fill is cached apart from a stroke";
}

TEST(UiDrawList, EqualGradientsShareARampRow) {
    Fixture f;
    engine::ui::Gradient red;
    red.kind = engine::ui::GradientKind::Linear;
    red.angle_deg = 90.0f;
    red.stops = {{{1, 0, 0, 1}, 0.0f}, {{0, 0, 0, 0}, 100.0f}};
    engine::ui::Gradient turned = red;
    turned.angle_deg = 180.0f;
    engine::ui::Gradient radial = red;
    radial.kind = engine::ui::GradientKind::Radial;
    f.painter.fill_rounded_rect_gradient({0, 0, 100, 20}, 0.0f, red);
    f.painter.fill_rounded_rect_gradient({0, 30, 100, 20}, 0.0f, turned);
    f.painter.fill_rounded_rect_gradient({0, 60, 100, 20}, 0.0f, radial);
    EXPECT_EQ(f.resources.ramps.row_count(), 1u) << "linear and radial ramps are their first and last stop";
    EXPECT_EQ(f.vertex(0, 0).aux.x, f.vertex(1, 0).aux.x);

    engine::ui::Gradient conic = red;
    conic.kind = engine::ui::GradientKind::Conic;
    f.painter.fill_rounded_rect_gradient({0, 90, 100, 100}, 0.0f, conic);
    engine::ui::Gradient blue = red;
    blue.stops[0].color = {0, 0, 1, 1};
    f.painter.fill_rounded_rect_gradient({0, 200, 100, 20}, 0.0f, blue);
    EXPECT_EQ(f.resources.ramps.row_count(), 3u);
    EXPECT_EQ(f.list().batches().size(), 1u);

    f.painter.begin_frame(800, 600);
    const std::uint64_t version = f.resources.ramps.version();
    f.painter.fill_rounded_rect_gradient({0, 0, 100, 20}, 0.0f, red);
    EXPECT_EQ(f.resources.ramps.version(), version) << "the next frame reuses the row";

    // The row: premultiplied, mixed in premultiplied space like nvgLinearGradient's inner and outer colors.
    const engine::ui::AtlasPage& page = f.resources.ramps.page(0);
    const std::uint8_t* row = page.pixels().data();
    EXPECT_EQ(row[0], 255);
    EXPECT_EQ(row[3], 255);
    EXPECT_EQ(row[255 * 4 + 0], 0);
    EXPECT_EQ(row[255 * 4 + 3], 0);
    EXPECT_NEAR(row[128 * 4 + 0], 127, 1);
    EXPECT_EQ(row[128 * 4 + 0], row[128 * 4 + 3]);
}

TEST(UiDrawList, GradientCoordinatesMatchThePaint) {
    Fixture f;
    engine::ui::Gradient g;
    g.stops = {{{1, 0, 0, 1}, 0.0f}, {{0, 0, 1, 1}, 100.0f}};
    g.kind = engine::ui::GradientKind::Linear;
    g.angle_deg = 90.0f;
    f.painter.fill_rounded_rect_gradient({0, 0, 100, 20}, 0.0f, g);
    EXPECT_NEAR(fragment_at(f.list(), 0, {0, 10}).uv.x, 0.0f, 1e-4f);
    EXPECT_NEAR(fragment_at(f.list(), 0, {25, 10}).uv.x, 0.25f, 1e-4f);
    EXPECT_NEAR(fragment_at(f.list(), 0, {100, 3}).uv.x, 1.0f, 1e-4f);

    g.kind = engine::ui::GradientKind::Radial;
    f.painter.fill_rounded_rect_gradient({0, 0, 100, 50}, 0.0f, g);
    const auto t_radial = [&](glm::vec2 p) {
        const UiFragment frag = fragment_at(f.list(), 1, p);
        return glm::length(frag.uv) + frag.aux.y;
    };
    EXPECT_NEAR(t_radial({50, 25}), 0.0f, 1e-4f);
    EXPECT_NEAR(t_radial({100, 25}), 1.0f, 1e-4f);

    g.kind = engine::ui::GradientKind::Conic;
    f.painter.fill_rounded_rect_gradient({0, 0, 100, 100}, 0.0f, g);
    const auto ramp_u = [&](glm::vec2 p) {
        return engine::ui::fragment_fetch(fragment_at(f.list(), 2, p))->uv.x;
    };
    constexpr float kW = engine::ui::kRampWidth;
    EXPECT_NEAR(ramp_u({50, 0}), 0.5f / kW, 1e-4f) << "0 at 12 o'clock";
    EXPECT_NEAR(ramp_u({100, 50}), (0.25f * (kW - 1.0f) + 0.5f) / kW, 1e-4f) << "a quarter at 3 o'clock";
    EXPECT_NEAR(ramp_u({50, 100}), (0.5f * (kW - 1.0f) + 0.5f) / kW, 1e-4f);
}

TEST(UiDrawList, ShapeCoverageFollowsTheOnePixelFringe) {
    Fixture f;
    f.painter.stroke_rounded_rect({10, 10, 100, 50}, 0.0f, 4.0f, {1, 1, 1, 1});
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 10}), 1.0f, 1e-4f) << "centered on the edge";
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 12}), 0.5f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 8}), 0.5f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {60, 13}), 0.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 0, {8, 8}), 0.5f, 1e-4f) << "a sharp rect's stroke has mitered corners";

    f.painter.draw_line({10, 100}, {110, 100}, {1, 1, 1, 1}, 2.0f);
    EXPECT_NEAR(alpha_at(f.list(), 1, {10, 100}), 0.5f, 1e-4f) << "butt cap at the end point";
    EXPECT_NEAR(alpha_at(f.list(), 1, {60, 101}), 0.5f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 1, {60, 100}), 1.0f, 1e-4f);

    f.painter.stroke_arc({200, 200}, 50.0f, 0.0f, kPi * 0.5f, 4.0f, {1, 1, 1, 1});
    const float d = 50.0f / std::sqrt(2.0f);
    EXPECT_NEAR(alpha_at(f.list(), 2, {200 + d, 200 - d}), 1.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 2, {200, 150}), 0.5f, 1e-3f) << "butt cap on the 12 o'clock radius";
    EXPECT_NEAR(alpha_at(f.list(), 2, {150, 200}), 0.0f, 1e-4f);
    EXPECT_NEAR(alpha_at(f.list(), 2, {200, 250}), 0.0f, 1e-4f);

    f.painter.fill_rounded_rect({300, 10, 100, 40}, 10.0f, {1, 1, 1, 1});
    const float corner = 10.0f - 10.0f / std::sqrt(2.0f);
    EXPECT_NEAR(alpha_at(f.list(), 3, {300 + corner, 10 + corner}), 0.5f, 1e-3f) << "round corner";
    EXPECT_NEAR(alpha_at(f.list(), 3, {300.5f, 10.5f}), 0.0f, 1e-4f);

    // Under a pixel wide: drawn one pixel wide, faded by the square of the width (nvgStroke).
    f.painter.draw_line({10, 300}, {110, 300}, {1, 1, 1, 1}, 0.5f);
    EXPECT_FLOAT_EQ(f.vertex(4, 0).shape.y, 0.5f);
    EXPECT_NEAR(engine::ui::unpack_color(f.vertex(4, 0).color).a, 0.25f, 1.0f / 255.0f);
    f.painter.save();
    f.painter.apply_transform({0, 0}, 0.0f, 2.0f);
    f.painter.draw_line({10, 300}, {110, 300}, {1, 1, 1, 1}, 0.5f);
    f.painter.restore();
    EXPECT_FLOAT_EQ(f.vertex(5, 0).shape.y, 0.5f);
    EXPECT_EQ(f.vertex(5, 0).color >> 24, 255u);
    EXPECT_EQ(f.list().batches().size(), 1u);
}

TEST(UiDrawList, TextQuadsMoveWithTheTransform) {
    Fixture f;
    f.painter.set_font(engine::builtin::font_ui, 16.0f);
    f.painter.fill_text("Hi there", {10, 10}, {1, 1, 1, 1}, engine::ui::UiAlign::Start, engine::ui::UiAlign::Start);
    const std::size_t glyphs = f.quads();
    EXPECT_EQ(glyphs, 7u) << "the space has no ink";
    f.painter.save();
    f.painter.apply_view({0, 0}, {10, 0}, 1.0f);
    f.painter.fill_text("Hi there", {10, 10}, {1, 1, 1, 1}, engine::ui::UiAlign::Start, engine::ui::UiAlign::Start);
    f.painter.restore();
    ASSERT_EQ(f.quads(), glyphs * 2);
    for (std::size_t q = 0; q < glyphs; ++q) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_NEAR(f.vertex(glyphs + q, i).position.x - f.vertex(q, i).position.x, 10.0f, 1e-4f);
            EXPECT_EQ(f.vertex(glyphs + q, i).uv, f.vertex(q, i).uv);
        }
    }
    EXPECT_EQ(f.vertex(0, 0).kind, UiShapeKind::Glyph);
    const auto fetch = engine::ui::fragment_fetch(engine::ui::fragment_of(f.vertex(0, 0)));
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->slot, UiTextureSlot::Glyph);
    EXPECT_EQ(f.list().batches().size(), 1u);
}

TEST(UiDrawList, ASteadySecondFrameAllocatesNothing) {
    Fixture f;
    draw_mixed_scene(f.painter);
    f.painter.end_frame();
    const std::size_t vertices = f.list().vertices().size();
    const std::size_t vertex_capacity = f.list().vertices().capacity();
    const std::size_t index_capacity = f.list().indices().capacity();
    const std::size_t batch_capacity = f.list().batches().capacity();
    const std::uint64_t glyphs = f.resources.fonts.atlas().version();
    const std::uint64_t masks = f.resources.masks.version();
    const std::uint64_t ramps = f.resources.ramps.version();

    f.painter.begin_frame(800, 600);
    draw_mixed_scene(f.painter);
    f.painter.end_frame();
    EXPECT_EQ(f.list().vertices().size(), vertices);
    EXPECT_EQ(f.list().vertices().capacity(), vertex_capacity);
    EXPECT_EQ(f.list().indices().capacity(), index_capacity);
    EXPECT_EQ(f.list().batches().capacity(), batch_capacity);
    EXPECT_EQ(f.resources.fonts.atlas().version(), glyphs);
    EXPECT_EQ(f.resources.masks.version(), masks);
    EXPECT_EQ(f.resources.ramps.version(), ramps);
    EXPECT_EQ(f.resources.masks.rasterized_count(), 1u);
}

TEST(UiDrawList, FragmentReferenceMatchesThePipelineContract) {
    static_assert(sizeof(UiVertex) == engine::ui::kUiVertexStride);
    EXPECT_EQ(engine::ui::kUiVertexAttributes.back().offset, 68u);
    EXPECT_EQ(engine::ui::pack_premultiplied({1, 1, 1, 0.5f}, 1.0f), 0x80808080u);
    EXPECT_EQ(engine::ui::pack_premultiplied({1, 0, 0, 1}, 1.0f), 0xFF0000FFu);
    // An elliptic corner (x and y radii clamped apart) is a true ellipse at its axes.
    EXPECT_NEAR(engine::ui::box_distance({0, 20}, {10, 20}, {10, 20}), 0.0f, 1e-5f);
    EXPECT_NEAR(engine::ui::box_distance({10, 0}, {10, 20}, {10, 20}), 0.0f, 1e-5f);
    EXPECT_LT(engine::ui::box_distance({5, 5}, {10, 20}, {10, 20}), 0.0f);
    UiFragment glyph;
    glyph.kind = UiShapeKind::Glyph;
    glyph.color = {0.5f, 0.5f, 0.5f, 0.5f};
    glyph.clip = {0, 0, 1e7f, 1e7f};
    const glm::vec4 out = engine::ui::shade_fragment(glyph, {0.5f, 0, 0, 0});
    EXPECT_FLOAT_EQ(out.a, 0.25f);
}

// --- A document, recorded into a Picture and replayed through the batcher ----------------------------------------

class EmptyVm final : public engine::ui::ViewModel {};

constexpr char kDocXml[] = R"(
<Canvas>
  <Stack class="hud">
    <Stack class="bar"><Label class="title" text="Score 1200"/></Stack>
    <Label class="plain" text="Lives 3"/>
    <Stack class="chip"/>
    <Stack class="meter"/>
  </Stack>
</Canvas>
)";

constexpr char kDocCss[] = R"(
.hud { gap: 4px; background: #101820; padding: 6px; }
.bar { height: 24px; background: linear-gradient(90deg, #203040, #405060); border-radius: 4px; }
.title, .plain { font-size: 14px; color: #ffffff; }
.chip { width: 20px; height: 20px; background: #884422; border-radius: 4px; border-width: 1px; border-color: #ffffff; }
.meter { width: 80px; height: 8px; background: #2266aa; opacity: 0.5; }
)";

TEST(UiDrawList, AReplayedPictureOfADocumentIsOneBatch) {
    Fixture f;
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(kDocCss, warnings);
    ASSERT_TRUE(sheet.has_value());
    EmptyVm vm;
    auto parsed = engine::ui::parse_xml(kDocXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument document = std::move(*parsed);
    ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &*sheet).has_value());
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 400.0f, 300.0f},
            .pointer = {-100.0f, -100.0f},
            .delta_time = 0.016f,
            .window_width = 400.0f,
            .window_height = 300.0f};

    engine::ui::Picture picture;
    engine::ui::RecordingPainter recording(f.painter, picture);
    engine::ui::paint_document(document, &*sheet, recording, input);
    ASSERT_FALSE(picture.empty());
    EXPECT_FALSE(recording.saw_empty_measure()) << "the batcher measures with the real font";

    f.painter.begin_frame(400, 300);
    picture.replay(f.painter);
    f.painter.end_frame();
    EXPECT_EQ(f.painter.queued_draw_calls(), 1);
    const std::size_t replayed = f.quads();
    EXPECT_GT(replayed, 15u) << "panels, a gradient, a border, and two labels' glyphs";

    bool saw_glyph = false;
    bool saw_gradient = false;
    bool saw_stroke = false;
    for (const UiVertex& v : f.list().vertices()) {
        saw_glyph = saw_glyph || v.kind == UiShapeKind::Glyph;
        saw_gradient = saw_gradient || v.kind == UiShapeKind::LinearGradient;
        saw_stroke = saw_stroke || v.kind == UiShapeKind::Stroke;
    }
    EXPECT_TRUE(saw_glyph);
    EXPECT_TRUE(saw_gradient);
    EXPECT_TRUE(saw_stroke);

    // Painting the document straight into the batcher gives the same geometry.
    f.painter.begin_frame(400, 300);
    engine::ui::paint_document(document, &*sheet, f.painter, input);
    EXPECT_EQ(f.quads(), replayed);
    EXPECT_EQ(f.painter.queued_draw_calls(), 1);
}

}
