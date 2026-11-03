#pragma once

// A LoopbackTransport with a scripted network in front of its sends: latency, jitter (which reorders),
// loss, and holding messages back to release them later. Time is virtual (`advance`), so tests stay
// deterministic. Reliable messages are never dropped and keep their order. Every send is recorded.

#include <engine/net/loopback_bus.h>
#include <engine/net/loopback_transport.h>
#include <engine/net/network_transport.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <vector>

namespace engine::net::testing {

class SimulatedLink final : public INetworkTransport {
public:
    struct Sent {
        PeerId target{kInvalidPeerId};
        std::vector<std::uint8_t> bytes{};
        DeliveryMode mode{DeliveryMode::ReliableOrdered};
        bool dropped{false};
    };

    // Index of the send among all sends of this link, and its bytes. True drops it (unreliable only).
    using DropFn = std::function<bool(std::size_t index, std::span<const std::uint8_t> bytes)>;

    explicit SimulatedLink(LoopbackBus& bus)
        : inner_(bus) {}

    void set_latency_ms(double latency) {
        latency_ms_ = latency;
    }
    // Each unreliable message waits an extra 0..jitter ms, from a seeded generator.
    void set_jitter_ms(std::uint32_t jitter, std::uint32_t seed = 1) {
        jitter_ms_ = jitter;
        random_.seed(seed);
    }
    void set_drop(DropFn drop) {
        drop_ = std::move(drop);
    }
    // While held, unreliable messages wait; `release` delivers them now, newest first when `reversed`.
    void hold() {
        holding_ = true;
    }
    void release(bool reversed) {
        holding_ = false;
        if (reversed) {
            std::reverse(held_.begin(), held_.end());
        }
        for (Pending& pending : held_) {
            pending.at = now_ms_;
            pending.order = next_order_++;
            queue_.push_back(std::move(pending));
        }
        held_.clear();
        deliver();
    }

    void advance(double ms) {
        now_ms_ += ms;
        deliver();
    }

    [[nodiscard]] const std::vector<Sent>& sent() const {
        return sent_;
    }

    bool start_server(std::uint16_t port) override {
        return inner_.start_server(port);
    }
    bool connect(std::string_view address, std::uint16_t port) override {
        return inner_.connect(address, port);
    }
    void disconnect(PeerId peer_id) override {
        inner_.disconnect(peer_id);
    }
    void stop() override {
        inner_.stop();
    }

    bool send(PeerId target, std::span<const std::uint8_t> data, DeliveryMode mode = DeliveryMode::ReliableOrdered,
            std::uint8_t channel = 0) override {
        (void)channel;
        const std::size_t index = sent_.size();
        const bool reliable = mode == DeliveryMode::ReliableOrdered;
        const bool dropped = !reliable && drop_ && drop_(index, data);
        sent_.push_back(Sent{.target = target, .bytes = {data.begin(), data.end()}, .mode = mode, .dropped = dropped});
        if (dropped) {
            return true;
        }
        Pending pending{
                .target = target, .bytes = {data.begin(), data.end()}, .mode = mode, .at = now_ms_ + latency_ms_};
        if (reliable) {
            pending.at = std::max(pending.at, last_reliable_at_);
            last_reliable_at_ = pending.at;
        } else {
            if (jitter_ms_ > 0) {
                pending.at += static_cast<double>(random_() % (jitter_ms_ + 1));
            }
            if (holding_) {
                held_.push_back(std::move(pending));
                return true;
            }
        }
        pending.order = next_order_++;
        queue_.push_back(std::move(pending));
        deliver();
        return true;
    }

    void poll_events(const EventCallback& callback) override {
        inner_.poll_events(callback);
    }

    [[nodiscard]] bool is_running() const override {
        return inner_.is_running();
    }
    [[nodiscard]] bool is_server() const override {
        return inner_.is_server();
    }
    [[nodiscard]] float rtt_ms(PeerId peer_id) const override {
        return inner_.rtt_ms(peer_id) < 0.0f ? -1.0f : static_cast<float>(2.0 * latency_ms_);
    }

private:
    struct Pending {
        PeerId target{kInvalidPeerId};
        std::vector<std::uint8_t> bytes{};
        DeliveryMode mode{DeliveryMode::ReliableOrdered};
        double at{0.0};
        std::uint64_t order{0};
    };

    void deliver() {
        std::stable_sort(queue_.begin(), queue_.end(), [](const Pending& a, const Pending& b) {
            return a.at != b.at ? a.at < b.at : a.order < b.order;
        });
        std::size_t due = 0;
        while (due < queue_.size() && queue_[due].at <= now_ms_) {
            ++due;
        }
        std::vector<Pending> ready(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(due));
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(due));
        for (const Pending& pending : ready) {
            inner_.send(pending.target, pending.bytes, pending.mode);
        }
    }

    LoopbackTransport inner_;
    double now_ms_{0.0};
    double latency_ms_{0.0};
    double last_reliable_at_{0.0};
    std::uint32_t jitter_ms_{0};
    std::minstd_rand random_{1};
    DropFn drop_{};
    bool holding_{false};
    std::uint64_t next_order_{0};
    std::vector<Pending> queue_{};
    std::vector<Pending> held_{};
    std::vector<Sent> sent_{};
};

} // namespace engine::net::testing
