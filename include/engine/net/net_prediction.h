#pragma once

// docs/tech/modules/Net.md

#include <engine/net/prediction_channel.h>

#include <memory>

namespace engine::net {

// `world.ctx<NetPrediction>()`: the world's prediction channel, set by `register_prediction`. Null without one.
struct NetPrediction {
    std::unique_ptr<PredictionChannel> channel{};
};

} // namespace engine::net
