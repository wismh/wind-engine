#include "mesh_check.h"

#include <cstddef>
#include <format>

namespace engine::render {

std::string mesh_desc_error(const MeshDesc& desc) {
    if (desc.vertices.empty()) {
        return "the mesh has no vertices";
    }
    if (desc.indices.size() % 3 != 0) {
        return std::format("the mesh has {} indices, not a multiple of 3", desc.indices.size());
    }
    const std::size_t vertex_count = desc.vertices.size();
    for (std::size_t i = 0; i < desc.indices.size(); ++i) {
        if (desc.indices[i] >= vertex_count) {
            return std::format("index {} of the mesh is {}, past its {} vertices", i, desc.indices[i], vertex_count);
        }
    }
    return {};
}

}
