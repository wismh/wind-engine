#include "net/net_schema.h"

#include <engine/net/net_prediction.h>
#include <engine/net/replication_registry.h>

#include <string>

namespace engine::net {

std::uint32_t welcome_schema_hash(ecs::World& world) {
    std::uint32_t hash = world.ctx<ReplicationRegistry>().schema_hash();
    const NetPrediction* prediction = world.try_ctx<NetPrediction>();
    if (prediction == nullptr || prediction->channel == nullptr) {
        return hash;
    }
    const std::string tag = "input:" + std::string(prediction->channel->name());
    for (const char c : tag) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace engine::net
