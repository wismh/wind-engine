#pragma once

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/bindable.h>
#include <engine/ui/canvas.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

namespace engine {

inline constexpr int kApiEpoch = 1;

int api_epoch();

}
