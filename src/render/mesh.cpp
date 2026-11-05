#include "mesh_check.h"

#include <engine/log.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

#include <glm/common.hpp>

#include <string>

namespace engine::render {

Aabb mesh_bounds(const MeshDesc& desc) {
    if (desc.vertices.empty()) {
        return {};
    }
    Aabb box{.min = desc.vertices.front().position, .max = desc.vertices.front().position};
    for (const MeshVertex& vertex : desc.vertices) {
        box.min = glm::min(box.min, vertex.position);
        box.max = glm::max(box.max, vertex.position);
    }
    return box;
}

namespace {

// `bounds_override` is the posed box of a skinned mesh. The vertex positions are the bind pose, which is smaller.
Aabb draw_bounds(const MeshDesc& desc) {
    if (desc.bounds_override) {
        return *desc.bounds_override;
    }
    return mesh_bounds(desc);
}

}

IMesh::IMesh(const MeshDesc& desc)
    : bounds_(draw_bounds(desc)) {}

bool IMesh::update(const MeshDesc& desc) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error("mesh update: " + error);
        return false;
    }
    if (!replace(desc)) {
        return false;
    }
    bounds_ = draw_bounds(desc);
    return true;
}

}
