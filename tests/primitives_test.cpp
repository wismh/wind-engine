#include <gtest/gtest.h>

#include <engine/render/primitives.h>

#include "render/mesh_check.h"
#include "resources/importers.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

constexpr float kEpsilon = 1e-5f;

std::optional<engine::render::MeshDesc> load_builtin_mesh(const char* name) {
    const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "meshes" / name;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return engine::parse_mesh(text);
}

// Valid, unit normals, UVs in [0, 1], and every triangle has area and winds counter-clockwise about its normal.
void expect_well_formed(const engine::render::MeshDesc& mesh) {
    EXPECT_EQ(engine::render::mesh_desc_error(mesh), "");
    ASSERT_FALSE(mesh.indices.empty());
    for (const engine::render::MeshVertex& vertex : mesh.vertices) {
        EXPECT_NEAR(glm::length(vertex.normal), 1.0f, kEpsilon);
        EXPECT_GE(vertex.uv.x, 0.0f);
        EXPECT_LE(vertex.uv.x, 1.0f);
        EXPECT_GE(vertex.uv.y, 0.0f);
        EXPECT_LE(vertex.uv.y, 1.0f);
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const engine::render::MeshVertex& a = mesh.vertices[mesh.indices[i]];
        const engine::render::MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
        const engine::render::MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
        ASSERT_GT(glm::length(face), 0.0f) << "triangle " << i / 3 << " has no area";
        const glm::vec3 normal = a.normal + b.normal + c.normal;
        EXPECT_GT(glm::dot(face, normal), 0.0f) << "triangle " << i / 3 << " winds clockwise";
    }
}

// Every normal points away from the origin, and lies along the position for a sphere.
void expect_outward(const engine::render::MeshDesc& mesh) {
    for (const engine::render::MeshVertex& vertex : mesh.vertices) {
        EXPECT_GT(glm::dot(vertex.normal, vertex.position), 0.0f);
    }
}

void expect_same_mesh(const engine::render::MeshDesc& actual, const engine::render::MeshDesc& expected) {
    ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
    EXPECT_EQ(actual.indices, expected.indices);
    for (std::size_t i = 0; i < actual.vertices.size(); ++i) {
        EXPECT_EQ(actual.vertices[i].position, expected.vertices[i].position) << "vertex " << i;
        EXPECT_EQ(actual.vertices[i].normal, expected.vertices[i].normal) << "vertex " << i;
        EXPECT_EQ(actual.vertices[i].uv, expected.vertices[i].uv) << "vertex " << i;
    }
}

}

TEST(Primitives, QuadMatchesBuiltinFile) {
    const engine::render::MeshDesc quad = engine::render::make_quad();
    EXPECT_EQ(quad.vertices.size(), 4u);
    EXPECT_EQ(quad.indices.size(), 6u);
    expect_well_formed(quad);
    for (const engine::render::MeshVertex& vertex : quad.vertices) {
        EXPECT_EQ(vertex.normal, glm::vec3(0.0f, 0.0f, 1.0f));
        EXPECT_FLOAT_EQ(vertex.position.z, 0.0f);
    }
    const auto file = load_builtin_mesh("quad.mesh");
    ASSERT_TRUE(file.has_value());
    expect_same_mesh(*file, quad);
}

TEST(Primitives, CubeMatchesBuiltinFile) {
    const engine::render::MeshDesc cube = engine::render::make_cube();
    EXPECT_EQ(cube.vertices.size(), 24u);
    EXPECT_EQ(cube.indices.size(), 36u);
    expect_well_formed(cube);
    expect_outward(cube);
    const auto file = load_builtin_mesh("cube.mesh");
    ASSERT_TRUE(file.has_value());
    expect_same_mesh(*file, cube);
}

TEST(Primitives, CubeScalesWithSize) {
    const engine::render::MeshDesc cube = engine::render::make_cube(3.0f);
    expect_well_formed(cube);
    for (const engine::render::MeshVertex& vertex : cube.vertices) {
        EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 1.5f);
        EXPECT_FLOAT_EQ(std::abs(vertex.position.y), 1.5f);
        EXPECT_FLOAT_EQ(std::abs(vertex.position.z), 1.5f);
        // Each vertex sits on the face its normal names.
        EXPECT_FLOAT_EQ(glm::dot(vertex.normal, vertex.position), 1.5f);
    }
}

TEST(Primitives, PlaneFacesUp) {
    const engine::render::MeshDesc plane = engine::render::make_plane(2.0f, 4);
    EXPECT_EQ(plane.vertices.size(), 25u);
    EXPECT_EQ(plane.indices.size(), 4u * 4u * 6u);
    expect_well_formed(plane);
    for (const engine::render::MeshVertex& vertex : plane.vertices) {
        EXPECT_EQ(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        EXPECT_FLOAT_EQ(vertex.position.y, 0.0f);
        EXPECT_LE(std::abs(vertex.position.x), 1.0f + kEpsilon);
        EXPECT_LE(std::abs(vertex.position.z), 1.0f + kEpsilon);
    }
    // U along +X, V along -Z.
    EXPECT_EQ(plane.vertices.front().position, glm::vec3(-1.0f, 0.0f, 1.0f));
    EXPECT_EQ(plane.vertices.front().uv, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(plane.vertices.back().position, glm::vec3(1.0f, 0.0f, -1.0f));
    EXPECT_EQ(plane.vertices.back().uv, glm::vec2(1.0f, 1.0f));
}

TEST(Primitives, PlaneClampsSubdivisions) {
    const engine::render::MeshDesc plane = engine::render::make_plane(1.0f, 0);
    EXPECT_EQ(plane.vertices.size(), 4u);
    EXPECT_EQ(plane.indices.size(), 6u);
    expect_well_formed(plane);
}

TEST(Primitives, UvSphere) {
    const engine::render::MeshDesc sphere = engine::render::make_uv_sphere(2.0f, 8, 4);
    EXPECT_EQ(sphere.vertices.size(), 9u * 5u);
    // Every band is two triangles per segment except the two at the poles, which are one.
    EXPECT_EQ(sphere.indices.size(), 8u * 3u * 6u);
    expect_well_formed(sphere);
    expect_outward(sphere);
    for (const engine::render::MeshVertex& vertex : sphere.vertices) {
        EXPECT_NEAR(glm::length(vertex.position), 2.0f, kEpsilon);
        EXPECT_NEAR(glm::length(vertex.normal * 2.0f - vertex.position), 0.0f, kEpsilon);
    }
    EXPECT_EQ(sphere.vertices.front().normal, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(sphere.vertices.back().normal, glm::vec3(0.0f, -1.0f, 0.0f));
}

TEST(Primitives, UvSphereDefaultsAndClamps) {
    const engine::render::MeshDesc sphere = engine::render::make_uv_sphere();
    EXPECT_EQ(sphere.vertices.size(), 33u * 17u);
    expect_well_formed(sphere);
    expect_outward(sphere);

    const engine::render::MeshDesc tiny = engine::render::make_uv_sphere(1.0f, 1, 1);
    EXPECT_EQ(tiny.vertices.size(), 4u * 3u);
    expect_well_formed(tiny);
}

TEST(Primitives, Cylinder) {
    const engine::render::MeshDesc cyl = engine::render::make_cylinder(1.0f, 2.0f, 8);
    expect_well_formed(cyl);
    expect_outward(cyl);
}

TEST(Primitives, Cone) {
    const engine::render::MeshDesc cone = engine::render::make_cone(1.0f, 2.0f, 8);
    expect_well_formed(cone);
    expect_outward(cone);
}

TEST(Primitives, Torus) {
    constexpr float kRingRadius = 1.0f;
    constexpr float kTubeRadius = 0.1f;
    const engine::render::MeshDesc torus = engine::render::make_torus(kRingRadius, kTubeRadius, 16, 8);
    expect_well_formed(torus);
    for (const engine::render::MeshVertex& vertex : torus.vertices) {
        const glm::vec3 ring_center =
                kRingRadius * glm::normalize(glm::vec3(vertex.position.x, 0.0f, vertex.position.z));
        EXPECT_GT(glm::dot(vertex.normal, vertex.position - ring_center), 0.0f);
    }
}


