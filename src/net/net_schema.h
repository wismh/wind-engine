#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/world.h>

#include <cstdint>

namespace engine::net {

// The schema hash Welcome carries: the replicated component names (`ReplicationRegistry::schema_hash`), then,
// when the world has a prediction channel, "input:" and its name. Server and client must agree.
[[nodiscard]] std::uint32_t welcome_schema_hash(ecs::World& world);

} // namespace engine::net
