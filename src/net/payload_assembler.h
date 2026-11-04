#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <engine/net/byte_stream.h>
#include <engine/net/net_payload_id.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace engine::net {

// Puts the client's incoming payloads back together. Memory grows only as bytes arrive, never from a size a message
// claims. The stream is reliable and ordered, so anything out of place means the two sides disagree and cannot recover:
// the caller disconnects.
class PayloadAssembler {
public:
    // More than this many unfinished payloads at once is malformed.
    static constexpr std::size_t kMaxOpen = 4096;

    struct Result {
        // A whole message, starting with its `MessageId`, ready for the dispatcher.
        struct Done {
            PayloadId id{kNoPayload};
            std::vector<std::uint8_t> message{};
        };

        std::optional<Done> done{};
        // Set when the message was malformed: why.
        std::optional<std::string> error{};
    };

    void set_limit(std::size_t max_payload_bytes) noexcept {
        limit_ = max_payload_bytes;
    }

    // Each reads what comes after the `MessageId`.
    [[nodiscard]] Result begin(ByteReader& reader);
    [[nodiscard]] Result fragment(ByteReader& reader);
    // A cancel of a payload this side already finished is normal; it does nothing.
    [[nodiscard]] Result cancel(ByteReader& reader);

    void clear() noexcept;

    [[nodiscard]] std::size_t open_count() const noexcept {
        return open_.size();
    }
    // Bytes held for unfinished payloads.
    [[nodiscard]] std::size_t held_bytes() const noexcept {
        return held_;
    }

private:
    struct Open {
        std::uint16_t message{0};
        bool compressed{false};
        std::uint32_t total{0};
        std::vector<std::uint8_t> bytes{};
    };

    [[nodiscard]] Result take_bytes(PayloadId id, Open& open, ByteReader& reader);
    [[nodiscard]] Result finish(PayloadId id, Open open);

    std::size_t limit_{0};
    std::size_t held_{0};
    std::map<PayloadId, Open> open_{};
};

} // namespace engine::net
