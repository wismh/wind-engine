#pragma once

namespace engine::render {

class ICanvas {
public:
    virtual ~ICanvas() = default;
    virtual void draw() = 0;
};

}
