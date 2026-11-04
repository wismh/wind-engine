#pragma once

// docs/tech/modules/Net.md

#include <engine/net/net_tick.h>

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace engine::net {

// One controlled entity's inputs on the server, by tick. The first copy of an input wins; the input for each
// server tick is taken once, in tick order. A tick with no input repeats the last one taken.
class InputBuffer {
public:
    enum class Insert : std::uint8_t {
        Stored,
        // Held already, or applied already.
        Duplicate,
        // Its tick ran already with a repeated input.
        Late,
        // More than `kMaxInputLeadTicks` after `server_tick`.
        TooFarAhead,
    };

    struct Taken {
        // The input to step with; empty before the first input (step with `Input{}`).
        std::span<const std::uint8_t> bytes{};
        // The input of this very tick, not a repeat.
        bool real{false};
        // A repeat of an earlier input (false before the first one).
        bool repeated{false};
    };

    // `resolved` is the newest tick that already ran without this buffer.
    explicit InputBuffer(Tick resolved) noexcept;

    Insert insert(Tick tick, std::span<const std::uint8_t> bytes, Tick server_tick);
    // The input for `tick`, which must be the tick after the previous `take`. Drops anything older.
    [[nodiscard]] Taken take(Tick tick);

    // The newest tick ever stored, `kNoTick` for none.
    [[nodiscard]] Tick newest() const noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;

private:
    std::map<Tick, std::vector<std::uint8_t>> pending_{};
    std::vector<std::uint8_t> last_{};
    bool has_last_{false};
    Tick resolved_{kNoTick};
    Tick newest_{kNoTick};
    // Bit i: tick `resolved_ - i` ran with its own input.
    std::uint64_t real_mask_{0};
};

} // namespace engine::net
