#pragma once

// docs/tech/modules/Render.md (Fog)

#include <glm/vec3.hpp>

namespace engine::render {

// Linear distance fog. On the `ActiveCamera` entity it turns fog on for that camera's views: `run_render` copies it
// into `CmdSetView::fog`. The builtin lit and unlit shaders mix the fragment's color towards `color` by
// `clamp((end - d) / (end - start), 0, 1)`, where `d` is the fragment's distance from the camera. Alpha is not fogged.
struct Fog {
    glm::vec3 color{1.0f};
    float start = 0.0f;
    float end = 100.0f;

    bool operator==(const Fog&) const = default;
};

}
