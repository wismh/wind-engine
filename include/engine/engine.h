#pragma once

#include <engine/audio/audio_system.h>
#include <engine/audio/sound.h>
#include <engine/core/application_state.h>
#include <engine/core/fixed_step.h>
#include <engine/core/host.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/builtin_ids.h>
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
