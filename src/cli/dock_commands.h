#pragma once

// docs/tech/features/CLI.md#dock

#include "cli/cli_server.h"

#include <engine/ecs/world.h>

#include <string>

namespace engine::cli {

    // `dock`: lists the dock spaces of the world, or (`request.action`) activates a tab, moves or floats a panel,
    // or switches a space's DockFloatMode. Layout changes go through DockLayout and bump DockSpace::revision, as a
    // user's do. The JSON body.
    [[nodiscard]] std::string dock_json(ecs::World &world, const CliRequest &request);

} // namespace engine::cli
