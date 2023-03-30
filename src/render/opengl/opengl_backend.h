#pragma once

#include <engine/render/backend.h>

namespace engine::render {

class OpenGLRenderBackend final : public IRenderBackend {
public:
    void execute(const CommandBuffer& commands) override;
};

}
