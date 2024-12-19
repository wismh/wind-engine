#pragma once

#include <engine/render/commands.h>
#include <engine/ui/draw_list.h>

#include <functional>

namespace engine::ui {

// Named paint contract on a ViewModel, parallel to `ICommand`. Invoked from `paint_document` after
// CSS chrome and before children. Not a `CommandBuffer` callback and not `CmdCustomDraw`.
class IPaint {
public:
    virtual ~IPaint() = default;

    virtual void paint(IDrawList& list, const render::Rect& content) = 0;
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
