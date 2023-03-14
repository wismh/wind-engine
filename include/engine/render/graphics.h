#pragma once

namespace engine::render {

class IMesh {
public:
    virtual ~IMesh() = default;
};

class IShader {
public:
    virtual ~IShader() = default;
};

class ITexture {
public:
    virtual ~ITexture() = default;
};

}
