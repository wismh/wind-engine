#pragma once

#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <variant>

namespace engine::ui {
struct Stylesheet;
struct UiDocument;
}

namespace engine::render {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr bool operator==(const Rect&) const noexcept = default;
};

struct CmdDrawMesh {
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct CmdDrawUI {
    Rect rect{};
    const ui::UiDocument* document = nullptr;
    const ui::Stylesheet* stylesheet = nullptr;
    glm::vec2 pointer{};
    bool pointer_down = false;
};

using Command = std::variant<CmdDrawMesh, CmdDrawUI>;

}
