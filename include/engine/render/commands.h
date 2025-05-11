#pragma once

// docs/tech/modules/Render.md

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace engine::ui {
    struct Stylesheet;
    struct UiDocument;
} // namespace engine::ui

namespace engine::render {

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        constexpr bool operator==(const Rect &) const noexcept = default;
    };

    struct CmdDrawMesh {
        std::shared_ptr<IMesh> mesh;
        std::shared_ptr<IMaterial> material;
        glm::mat4 model{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec2 uv_scale{1.0f, 1.0f};
        glm::vec2 uv_offset{0.0f, 0.0f};
        std::optional<MaterialOverride> material_override;
    };

    struct CmdDrawUI {
        Rect rect{};
        ui::UiDocument *document = nullptr;
        const ui::Stylesheet *stylesheet = nullptr;
        glm::vec2 pointer{};
        bool pointer_down = false;
        float delta_time = 0.0f;
        float window_width = 0.0f;
        float window_height = 0.0f;
        glm::vec2 ui_offset{0.0f, 0.0f}; // layout-space -> real-pixel offset (ScaleWithScreenSize letterbox)
        float ui_scale = 1.0f; // layout-space -> real-pixel scale
        // UI inspector overlay. Empty unless that canvas's world is inspected and it draws a hover or
        // selection box.
        bool inspector_hover = false;
        bool inspector_selection = false;
        std::vector<std::size_t> inspector_selection_path;
        const void *inspector_selection_owner = nullptr;
        // Which canvas this draw is, set only while its world is the one the UI profiler records. Paint
        // is attributed to it. Present in every configuration so the command layout does not change.
        ecs::Entity canvas{};
        // The window in layout space: where this canvas's popups may go. Empty means `rect`.
        Rect popup_bounds{};
        // Second command for the same canvas, pushed after every canvas of the window: draws only its open
        // popups, above everything else in the window.
        bool popup_layer = false;
    };

    struct ParticleInstance {
        glm::vec3 position{0.0f};
        float rotation = 0.0f;
        glm::vec2 size{1.0f, 1.0f};
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec2 uv_scale{1.0f, 1.0f};
        glm::vec2 uv_offset{0.0f, 0.0f};

        constexpr bool operator==(const ParticleInstance &) const noexcept = default;
    };

    struct CmdDrawParticles {
        std::shared_ptr<IMesh> mesh;
        std::shared_ptr<IMaterial> material;
        std::vector<ParticleInstance> instances;
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        BlendMode blend = BlendMode::Alpha;
    };

    using Command = std::variant<CmdDrawMesh, CmdDrawUI, CmdDrawParticles>;

} // namespace engine::render
