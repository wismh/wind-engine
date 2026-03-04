#pragma once

#include <engine/ui/paint.h>

#include <cstdint>

namespace bench {

// One stroked arc, three quarters of a ring, centred in the element: the paint-mix arc cell.
class ArcPaint final : public engine::ui::IPaint {
public:
    void paint(engine::ui::IDrawList& list, const engine::render::Rect& content) override;
    // What it draws never changes after construction: a picture of it is replayed until the end.
    [[nodiscard]] std::uint64_t version() const noexcept override { return 1; }
};

}
