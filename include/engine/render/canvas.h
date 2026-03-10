#pragma once

// docs/tech/modules/Render.md

namespace engine::render {

class ICanvas {
public:
    virtual ~ICanvas() = default;
    virtual void draw() = 0;
};

}
