#pragma once

// docs/tech/modules/UI.md

#include <engine/render/commands.h>
#include <engine/ui/draw_list.h>

#include <cstdint>
#include <functional>

namespace engine::ui {

// Named paint contract on a ViewModel, parallel to `ICommand`. Invoked from `paint_document` after
// CSS chrome and before children. Not a `CommandBuffer` callback and not `CmdCustomDraw`.
class IPaint {
public:
    virtual ~IPaint() = default;

    virtual void paint(IDrawList& list, const render::Rect& content) = 0;

    // A paint that can say when what it draws changed lets its canvas keep the picture of the element it paints on:
    // `version()` goes up whenever `paint` would draw something different from the last time, exactly like
    // `Bindable::version`. The UI compares it with the number it saw when it recorded the picture and records again
    // only when it moved. The default, `kVersionUnknown`, means "may differ every frame": the element is painted again
    // each frame, as before pictures existed.
    static constexpr std::uint64_t kVersionUnknown = ~std::uint64_t{0};
    [[nodiscard]] virtual std::uint64_t version() const noexcept { return kVersionUnknown; }
};

class RelayPaint final : public IPaint {
public:
    RelayPaint() = default;

    explicit RelayPaint(std::function<void(IDrawList&, const render::Rect&)> paint)
        : paint_(std::move(paint)) {}

    RelayPaint& operator=(std::function<void(IDrawList&, const render::Rect&)> paint) {
        paint_ = std::move(paint);
        return *this;
    }

    void paint(IDrawList& list, const render::Rect& content) override {
        if (paint_) {
            paint_(list, content);
        }
    }

private:
    std::function<void(IDrawList&, const render::Rect&)> paint_{};
};

}
