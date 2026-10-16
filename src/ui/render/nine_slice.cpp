#include "ui/render/nine_slice.h"

#include <algorithm>

namespace engine::ui {

NineSlicePieces nine_slice_pieces(const render::Rect& rect, const BoxInsets& insets, glm::vec2 texture_size) noexcept {
    NineSlicePieces out;
    const float tw = texture_size.x;
    const float th = texture_size.y;
    if (rect.w <= 0.0f || rect.h <= 0.0f || tw <= 0.0f || th <= 0.0f) {
        return out;
    }
    float sl = std::max(0.0f, insets.left);
    float sr = std::max(0.0f, insets.right);
    float st = std::max(0.0f, insets.top);
    float sb = std::max(0.0f, insets.bottom);
    if (sl + sr > tw) {
        const float k = tw / (sl + sr);
        sl *= k;
        sr *= k;
    }
    if (st + sb > th) {
        const float k = th / (st + sb);
        st *= k;
        sb *= k;
    }
    float dl = sl;
    float dr = sr;
    float dt = st;
    float db = sb;
    if (dl + dr > rect.w) {
        const float k = rect.w / (dl + dr);
        dl *= k;
        dr *= k;
    }
    if (dt + db > rect.h) {
        const float k = rect.h / (dt + db);
        dt *= k;
        db *= k;
    }
    const float src_x[3] = {0.0f, sl, tw - sr};
    const float src_w[3] = {sl, std::max(0.0f, tw - sl - sr), sr};
    const float dst_x[3] = {rect.x, rect.x + dl, rect.x + rect.w - dr};
    const float dst_w[3] = {dl, std::max(0.0f, rect.w - dl - dr), dr};
    const float src_y[3] = {0.0f, st, th - sb};
    const float src_h[3] = {st, std::max(0.0f, th - st - sb), sb};
    const float dst_y[3] = {rect.y, rect.y + dt, rect.y + rect.h - db};
    const float dst_h[3] = {dt, std::max(0.0f, rect.h - dt - db), db};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (dst_w[col] <= 0.0f || dst_h[row] <= 0.0f || src_w[col] <= 0.0f || src_h[row] <= 0.0f) {
                continue;
            }
            const float scale_x = dst_w[col] / src_w[col];
            const float scale_y = dst_h[row] / src_h[row];
            out.pieces[out.count++] = NineSlicePiece{{dst_x[col], dst_y[row], dst_w[col], dst_h[row]},
                    {dst_x[col] - src_x[col] * scale_x, dst_y[row] - src_y[row] * scale_y},
                    {tw * scale_x, th * scale_y}};
        }
    }
    return out;
}

}
