#pragma once

#include <engine/render/material.h>

#include <utility>

namespace engine::render {

class Material final : public IMaterial {
public:
    Material(std::shared_ptr<IShader> shader, std::shared_ptr<ITexture> albedo, glm::vec4 color, BlendMode blend)
        : shader_(std::move(shader))
        , albedo_(std::move(albedo))
        , color_(color)
        , blend_(blend) {}

    std::shared_ptr<IShader> shader() const override {
        return shader_;
    }

    std::shared_ptr<ITexture> texture(int slot) const override {
        if (slot != 0) {
            return {};
        }
        return albedo_;
    }

    glm::vec4 color() const override {
        return color_;
    }

    BlendMode blend() const override {
        return blend_;
    }

private:
    std::shared_ptr<IShader> shader_;
    std::shared_ptr<ITexture> albedo_;
    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    BlendMode blend_ = BlendMode::Opaque;
};

}
