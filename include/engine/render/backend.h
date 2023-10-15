#pragma once

#include <engine/render/command_buffer.h>

namespace engine::render {

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual void execute(const CommandBuffer& commands) = 0;
};

}
