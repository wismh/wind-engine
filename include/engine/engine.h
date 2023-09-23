#pragma once

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>

namespace engine {

inline constexpr int kApiEpoch = 1;

int api_epoch();

}
