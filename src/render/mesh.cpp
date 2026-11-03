#include "mesh_check.h"

#include <engine/log.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

#include <string>

namespace engine::render {

bool IMesh::update(const MeshDesc& desc) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error("mesh update: " + error);
        return false;
    }
    return replace(desc);
}

}
