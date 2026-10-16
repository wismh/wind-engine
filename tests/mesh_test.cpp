#include <gtest/gtest.h>

#include <engine/render/graphic_factory.h>

#include "render/mesh_check.h"
#include "resources/importers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

engine::render::MeshDesc triangle() {
    engine::render::MeshDesc desc;
    desc.vertices.resize(3);
    return desc;
}

}

TEST(Mesh, VertexNormalDefaultsToPlusZ) {
    const engine::render::MeshVertex vertex;
    EXPECT_EQ(vertex.normal, glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(Mesh, CheckAcceptsNonIndexedAndIndexed) {
    engine::render::MeshDesc desc = triangle();
    EXPECT_TRUE(engine::render::mesh_desc_error(desc).empty());
    desc.indices = {0, 1, 2, 2, 1, 0};
    EXPECT_TRUE(engine::render::mesh_desc_error(desc).empty());
}

TEST(Mesh, CheckRejectsEmptyMesh) {
    EXPECT_FALSE(engine::render::mesh_desc_error(engine::render::MeshDesc{}).empty());
}

TEST(Mesh, CheckRejectsIndexPastVertices) {
    engine::render::MeshDesc desc = triangle();
    desc.indices = {0, 1, 3};
    const std::string error = engine::render::mesh_desc_error(desc);
    EXPECT_NE(error.find("past its 3 vertices"), std::string::npos) << error;
}

TEST(Mesh, CheckRejectsPartialTriangle) {
    engine::render::MeshDesc desc = triangle();
    desc.indices = {0, 1};
    EXPECT_FALSE(engine::render::mesh_desc_error(desc).empty());
}

TEST(Mesh, ParseLegacyFiveColumns) {
    const auto desc = engine::parse_mesh("# pos uv\n"
                                         "-0.5 0.5 0  0 1\n"
                                         "-0.5 -0.5 0 0 0   # comment\n"
                                         "0.5 0.5 0 1 1\n");
    ASSERT_TRUE(desc.has_value());
    ASSERT_EQ(desc->vertices.size(), 3u);
    EXPECT_TRUE(desc->indices.empty());
    EXPECT_EQ(desc->vertices[1].position, glm::vec3(-0.5f, -0.5f, 0.0f));
    EXPECT_EQ(desc->vertices[1].normal, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(desc->vertices[2].uv, glm::vec2(1.0f, 1.0f));
}

TEST(Mesh, ParseNormalsAndFaces) {
    const auto desc = engine::parse_mesh("0 0 0   0 1 0   0 0\n"
                                         "1 0 0   0 1 0   1 0\n"
                                         "1 0 -1  0 1 0   1 1\n"
                                         "0 0 -1  0 1 0   0 1\n"
                                         "f 0 1 2\n"
                                         "  f 0 2 3  # second\n");
    ASSERT_TRUE(desc.has_value());
    ASSERT_EQ(desc->vertices.size(), 4u);
    EXPECT_EQ(desc->vertices[2].position, glm::vec3(1.0f, 0.0f, -1.0f));
    EXPECT_EQ(desc->vertices[2].normal, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(desc->vertices[2].uv, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(desc->indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
}

TEST(Mesh, ParseMixesFiveAndEightColumns) {
    const auto desc = engine::parse_mesh("0 0 0 0 0\n1 0 0  1 0 0  1 0\n0 1 0 0 1\n");
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->vertices[0].normal, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(desc->vertices[1].normal, glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(Mesh, ParseRejectsIndexPastVertices) {
    EXPECT_FALSE(engine::parse_mesh("0 0 0 0 0\n1 0 0 1 0\n0 1 0 0 1\nf 0 1 3\n").has_value());
}

TEST(Mesh, ParseRejectsMalformedRows) {
    constexpr const char* kVertices = "0 0 0 0 0\n1 0 0 1 0\n0 1 0 0 1\n";
    const auto with = [&](const char* row) { return engine::parse_mesh(std::string(kVertices) + row); };
    EXPECT_TRUE(with("f 0 1 2\n").has_value());
    EXPECT_FALSE(with("1 2 3 4\n").has_value()); // too few columns
    EXPECT_FALSE(with("1 2 3 4 5 6\n").has_value()); // neither 5 nor 8
    EXPECT_FALSE(with("1 2 3 4 5 6 7 8 9\n").has_value());
    EXPECT_FALSE(with("1 2 3 4 x\n").has_value()); // not a number
    EXPECT_FALSE(with("f 0 1\n").has_value()); // two corners
    EXPECT_FALSE(with("f 0 1 2 3\n").has_value()); // a quad is two rows
    EXPECT_FALSE(with("f 0 -1 2\n").has_value()); // negative index
    EXPECT_FALSE(with("f 0 1 1.5\n").has_value()); // not an integer
    EXPECT_FALSE(with("face 0 1 2\n").has_value()); // unknown tag
    EXPECT_FALSE(with("v 0 0 0 0 0\n").has_value());
}

TEST(Mesh, ParseRejectsFacesWithoutVertices) {
    EXPECT_FALSE(engine::parse_mesh("# nothing\n\n").has_value());
    EXPECT_FALSE(engine::parse_mesh("f 0 0 0\n").has_value());
}
