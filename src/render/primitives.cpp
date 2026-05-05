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

MeshDesc make_cylinder(float radius, float height, int segments) {
    const int around = std::max(segments, 3);
    const float h = height * 0.5f;
    MeshDesc mesh;
    const std::size_t side_verts = static_cast<std::size_t>(around + 1) * 2;
    mesh.vertices.reserve(side_verts + (static_cast<std::size_t>(around) + 1) * 2);
    mesh.indices.reserve(static_cast<std::size_t>(around) * 6 + static_cast<std::size_t>(around) * 6);

    for (int i = 0; i <= around; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(around);
        const float theta = glm::two_pi<float>() * u;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);
        const glm::vec3 normal{sin_t, 0.0f, cos_t};
        mesh.vertices.push_back({.position = {radius * sin_t, -h, radius * cos_t}, .normal = normal, .uv = {u, 0.0f}});
        mesh.vertices.push_back({.position = {radius * sin_t, h, radius * cos_t}, .normal = normal, .uv = {u, 1.0f}});
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(around); ++i) {
        const std::uint32_t lower_left = i * 2;
        const std::uint32_t upper_left = lower_left + 1;
        const std::uint32_t lower_right = lower_left + 2;
        const std::uint32_t upper_right = lower_left + 3;
        mesh.indices.insert(mesh.indices.end(), {lower_left, lower_right, upper_right});
        mesh.indices.insert(mesh.indices.end(), {lower_left, upper_right, upper_left});
    }

    const auto top_center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({.position = {0.0f, h, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.5f, 0.5f}});
    for (int i = 0; i < around; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(around);
        const float theta = glm::two_pi<float>() * u;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);
        mesh.vertices.push_back({
                .position = {radius * sin_t, h, radius * cos_t},
                .normal = {0.0f, 1.0f, 0.0f},
                .uv = {0.5f + 0.5f * sin_t, 0.5f + 0.5f * cos_t},
        });
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(around); ++i) {
        const std::uint32_t cur = top_center + 1 + i;
        const std::uint32_t next = top_center + 1 + (i + 1) % around;
        mesh.indices.insert(mesh.indices.end(), {top_center, cur, next});
    }

    const auto bot_center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({.position = {0.0f, -h, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uv = {0.5f, 0.5f}});
    for (int i = 0; i < around; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(around);
        const float theta = glm::two_pi<float>() * u;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);
        mesh.vertices.push_back({
                .position = {radius * sin_t, -h, radius * cos_t},
                .normal = {0.0f, -1.0f, 0.0f},
                .uv = {0.5f + 0.5f * sin_t, 0.5f + 0.5f * cos_t},
        });
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(around); ++i) {
        const std::uint32_t cur = bot_center + 1 + i;
        const std::uint32_t next = bot_center + 1 + (i + 1) % around;
        mesh.indices.insert(mesh.indices.end(), {bot_center, next, cur});
    }

    return mesh;
}

MeshDesc make_cone(float radius, float height, int segments) {
    const int around = std::max(segments, 3);
    const float h = height * 0.5f;
    MeshDesc mesh;

    const float slant_len = std::sqrt(4.0f * h * h + radius * radius);
    const float ny = radius / slant_len;
    const float n_xz = (2.0f * h) / slant_len;

    for (int i = 0; i <= around; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(around);
        const float theta = glm::two_pi<float>() * u;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);
        const glm::vec3 normal{n_xz * sin_t, ny, n_xz * cos_t};
        mesh.vertices.push_back({.position = {radius * sin_t, -h, radius * cos_t}, .normal = normal, .uv = {u, 0.0f}});
        mesh.vertices.push_back({.position = {0.0f, h, 0.0f}, .normal = normal, .uv = {u, 1.0f}});
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(around); ++i) {
        const std::uint32_t lower_left = i * 2;
        const std::uint32_t upper = lower_left + 1;
        const std::uint32_t lower_right = lower_left + 2;
        mesh.indices.insert(mesh.indices.end(), {lower_left, lower_right, upper});
    }

    const auto bot_center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({.position = {0.0f, -h, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}, .uv = {0.5f, 0.5f}});
    for (int i = 0; i < around; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(around);
        const float theta = glm::two_pi<float>() * u;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);
        mesh.vertices.push_back({
                .position = {radius * sin_t, -h, radius * cos_t},
                .normal = {0.0f, -1.0f, 0.0f},
                .uv = {0.5f + 0.5f * sin_t, 0.5f + 0.5f * cos_t},
        });
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(around); ++i) {
        const std::uint32_t cur = bot_center + 1 + i;
        const std::uint32_t next = bot_center + 1 + (i + 1) % around;
        mesh.indices.insert(mesh.indices.end(), {bot_center, next, cur});
    }

    return mesh;
}

MeshDesc make_torus(float ring_radius, float tube_radius, int ring_segments, int tube_segments) {
    const int num_ring = std::max(ring_segments, 3);
    const int num_tube = std::max(tube_segments, 3);
    MeshDesc mesh;

    mesh.vertices.reserve(static_cast<std::size_t>((num_ring + 1) * (num_tube + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(num_ring * num_tube * 6));

    for (int i = 0; i <= num_ring; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(num_ring);
        const float phi = glm::two_pi<float>() * u;
        const float cos_phi = std::cos(phi);
        const float sin_phi = std::sin(phi);

        const glm::vec3 center{ring_radius * cos_phi, 0.0f, ring_radius * sin_phi};
        const glm::vec3 dir_out{cos_phi, 0.0f, sin_phi};
        const glm::vec3 dir_up{0.0f, 1.0f, 0.0f};

        for (int j = 0; j <= num_tube; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(num_tube);
            const float theta = glm::two_pi<float>() * v;
            const float cos_theta = std::cos(theta);
            const float sin_theta = std::sin(theta);

            const glm::vec3 normal = cos_theta * dir_out + sin_theta * dir_up;
            const glm::vec3 pos = center + tube_radius * normal;

            mesh.vertices.push_back({.position = pos, .normal = normal, .uv = {u, v}});
        }
    }

    const auto stride = static_cast<std::uint32_t>(num_tube + 1);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(num_ring); ++i) {
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(num_tube); ++j) {
            const std::uint32_t p0 = i * stride + j;
            const std::uint32_t p1 = (i + 1) * stride + j;
            const std::uint32_t p2 = (i + 1) * stride + (j + 1);
            const std::uint32_t p3 = i * stride + (j + 1);

            mesh.indices.insert(mesh.indices.end(), {p0, p3, p1, p1, p3, p2});
        }
    }

    return mesh;
}

}

