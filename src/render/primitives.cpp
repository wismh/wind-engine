#include <engine/render/primitives.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::render {
namespace {

// One quad of four vertices: (-u-v, +u-v, +u+v, -u+v) around `center`, so u × v = normal gives counter-clockwise
// triangles 0-1-2 and 0-2-3.
void add_face(MeshDesc& mesh, glm::vec3 center, glm::vec3 normal, glm::vec3 u, glm::vec3 v) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({.position = center - u - v, .normal = normal, .uv = {0.0f, 0.0f}});
    mesh.vertices.push_back({.position = center + u - v, .normal = normal, .uv = {1.0f, 0.0f}});
    mesh.vertices.push_back({.position = center + u + v, .normal = normal, .uv = {1.0f, 1.0f}});
    mesh.vertices.push_back({.position = center - u + v, .normal = normal, .uv = {0.0f, 1.0f}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

}

MeshDesc make_quad() {
    // Same vertex order as builtin_assets/meshes/quad.mesh: top-left, bottom-left, top-right, bottom-right.
    MeshDesc mesh;
    const glm::vec3 normal{0.0f, 0.0f, 1.0f};
    mesh.vertices = {
            {.position = {-0.5f, 0.5f, 0.0f}, .normal = normal, .uv = {0.0f, 1.0f}},
            {.position = {-0.5f, -0.5f, 0.0f}, .normal = normal, .uv = {0.0f, 0.0f}},
            {.position = {0.5f, 0.5f, 0.0f}, .normal = normal, .uv = {1.0f, 1.0f}},
            {.position = {0.5f, -0.5f, 0.0f}, .normal = normal, .uv = {1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 1, 3, 2};
    return mesh;
}

MeshDesc make_cube(float size) {
    const float h = size * 0.5f;
    MeshDesc mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    // +X, -X, +Y, -Y, +Z, -Z: the order of builtin_assets/meshes/cube.mesh.
    add_face(mesh, {h, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -h}, {0.0f, h, 0.0f});
    add_face(mesh, {-h, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, h}, {0.0f, h, 0.0f});
    add_face(mesh, {0.0f, h, 0.0f}, {0.0f, 1.0f, 0.0f}, {h, 0.0f, 0.0f}, {0.0f, 0.0f, -h});
    add_face(mesh, {0.0f, -h, 0.0f}, {0.0f, -1.0f, 0.0f}, {h, 0.0f, 0.0f}, {0.0f, 0.0f, h});
    add_face(mesh, {0.0f, 0.0f, h}, {0.0f, 0.0f, 1.0f}, {h, 0.0f, 0.0f}, {0.0f, h, 0.0f});
    add_face(mesh, {0.0f, 0.0f, -h}, {0.0f, 0.0f, -1.0f}, {-h, 0.0f, 0.0f}, {0.0f, h, 0.0f});
    return mesh;
}

MeshDesc make_plane(float size, int subdivisions) {
    const int cells = std::max(subdivisions, 1);
    const auto side = static_cast<std::uint32_t>(cells + 1);
    const float h = size * 0.5f;
    MeshDesc mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(side) * side);
    mesh.indices.reserve(static_cast<std::size_t>(cells) * cells * 6);
    for (int row = 0; row <= cells; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(cells);
        for (int column = 0; column <= cells; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(cells);
            mesh.vertices.push_back({
                    .position = {-h + u * size, 0.0f, h - v * size},
                    .normal = {0.0f, 1.0f, 0.0f},
                    .uv = {u, v},
            });
        }
    }
    // U along +X and V along -Z: +X × -Z = +Y, so (a, b, c) and (a, c, d) wind counter-clockwise seen from above.
    for (std::uint32_t row = 0; row + 1 < side; ++row) {
        for (std::uint32_t column = 0; column + 1 < side; ++column) {
            const std::uint32_t a = row * side + column;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = b + side;
            const std::uint32_t d = a + side;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

MeshDesc make_uv_sphere(float radius, int segments, int rings) {
    const int around = std::max(segments, 3);
    const int down = std::max(rings, 2);
    const auto columns = static_cast<std::uint32_t>(around + 1);
    MeshDesc mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(down + 1));
    mesh.indices.reserve(static_cast<std::size_t>(around) * static_cast<std::size_t>(down - 1) * 6);
    for (int ring = 0; ring <= down; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(down);
        const float phi = glm::pi<float>() * v;
        // The poles are exact so their normal is exactly ±Y.
        const float y = ring == 0 ? 1.0f : (ring == down ? -1.0f : std::cos(phi));
        const float ring_radius = (ring == 0 || ring == down) ? 0.0f : std::sin(phi);
        for (int segment = 0; segment <= around; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(around);
            const float theta = glm::two_pi<float>() * u;
            const glm::vec3 normal{ring_radius * std::sin(theta), y, ring_radius * std::cos(theta)};
            mesh.vertices.push_back({.position = normal * radius, .normal = normal, .uv = {u, 1.0f - v}});
        }
    }
    // Ring r is above ring r + 1. Going around, U grows towards the right seen from outside, V upward: the lower-left,
    // lower-right, upper-right, upper-left corners wind counter-clockwise. The triangle that would touch a pole twice
    // has no area and is left out.
    for (std::uint32_t ring = 0; ring < static_cast<std::uint32_t>(down); ++ring) {
        for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(around); ++segment) {
            const std::uint32_t upper_left = ring * columns + segment;
            const std::uint32_t upper_right = upper_left + 1;
            const std::uint32_t lower_left = upper_left + columns;
            const std::uint32_t lower_right = lower_left + 1;
            if (ring + 1 < static_cast<std::uint32_t>(down)) {
                mesh.indices.insert(mesh.indices.end(), {lower_left, lower_right, upper_right});
            }
            if (ring > 0) {
                mesh.indices.insert(mesh.indices.end(), {lower_left, upper_right, upper_left});
            }
        }
    }
    return mesh;
}

}
