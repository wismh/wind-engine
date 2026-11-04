#include "net/payload_assembler.h"

#include <engine/core/compression.h>
#include <engine/net/net_messages.h>

#include <algorithm>
#include <string>
#include <utility>

namespace engine::net {

namespace {

constexpr std::uint8_t kPayloadFlagCompressed = 1u << 0;

[[nodiscard]] PayloadAssembler::Result failed(std::string why) {
    PayloadAssembler::Result result;
    result.error = std::move(why);
    return result;
}

} // namespace

PayloadAssembler::Result PayloadAssembler::begin(ByteReader& reader) {
    const auto id = reader.read_u32();
    const auto message = reader.read_u16();
    const auto flags = reader.read_u8();
    const auto total = reader.read_u32();
    if (!id || !message || !flags || !total) {
        return failed("truncated PayloadBegin");
    }
    if (*id == kNoPayload) {
        return failed("PayloadBegin with no id");
    }
    if (*message == kInvalidMessageId || (*message >= kEngineMessageFirst && *message <= kEngineMessageLast)) {
        return failed("PayloadBegin for message " + std::to_string(*message));
    }
    if ((*flags & ~kPayloadFlagCompressed) != 0) {
        return failed("PayloadBegin with unknown flags");
    }
    if (*total > limit_) {
        return failed("payload of " + std::to_string(*total) + " bytes is over the limit");
    }
    if (open_.contains(*id)) {
        return failed("PayloadBegin for a payload already open");
    }
    if (open_.size() >= kMaxOpen) {
        return failed("too many unfinished payloads");
    }
    Open open{.message = *message, .compressed = (*flags & kPayloadFlagCompressed) != 0, .total = *total};
    return take_bytes(*id, open_.emplace(*id, std::move(open)).first->second, reader);
}

PayloadAssembler::Result PayloadAssembler::fragment(ByteReader& reader) {
    const auto id = reader.read_u32();
    const auto offset = reader.read_u32();
    if (!id || !offset) {
        return failed("truncated PayloadFragment");
    }
    const auto at = open_.find(*id);
    if (at == open_.end()) {
        return failed("PayloadFragment for a payload that is not open");
    }
    if (*offset != at->second.bytes.size()) {
        return failed("PayloadFragment at offset " + std::to_string(*offset) + ", expected " +
                      std::to_string(at->second.bytes.size()));
    }
    return take_bytes(*id, at->second, reader);
}

PayloadAssembler::Result PayloadAssembler::cancel(ByteReader& reader) {
    const auto id = reader.read_u32();
    if (!id || reader.remaining() != 0) {
        return failed("malformed PayloadCancel");
    }
    if (const auto at = open_.find(*id); at != open_.end()) {
        held_ -= at->second.bytes.size();
        open_.erase(at);
    }
    return {};
}

PayloadAssembler::Result PayloadAssembler::take_bytes(PayloadId id, Open& open, ByteReader& reader) {
    const std::size_t data = reader.remaining();
    if (data > open.total - open.bytes.size()) {
        return failed("payload bytes beyond its size");
    }
    if (held_ + data > limit_) {
        return failed("unfinished payloads over the limit");
    }
    const auto bytes = reader.read_bytes(data);
    if (!bytes.has_value()) {
        return failed("truncated payload bytes");
    }
    open.bytes.insert(open.bytes.end(), bytes->begin(), bytes->end());
    held_ += data;
    if (open.bytes.size() < open.total) {
        return {};
    }
    held_ -= open.bytes.size();
    const auto at = open_.find(id);
    Open whole = std::move(at->second);
    open_.erase(at);
    return finish(id, std::move(whole));
}

PayloadAssembler::Result PayloadAssembler::finish(PayloadId id, Open open) {
    std::span<const std::uint8_t> raw = open.bytes;
    std::vector<std::uint8_t> decompressed;
    if (open.compressed) {
        auto unpacked = decompress(open.bytes, limit_);
        if (!unpacked.has_value()) {
            return failed("payload does not decompress: " + std::string(to_string(unpacked.error())));
        }
        decompressed = std::move(*unpacked);
        raw = decompressed;
    }
    ByteWriter message(sizeof(MessageId) + raw.size());
    message.write_u16(open.message);
    message.append(raw);
    Result result;
    result.done = Result::Done{.id = id, .message = message.buffer()};
    return result;
}

void PayloadAssembler::clear() noexcept {
    open_.clear();
    held_ = 0;
}

} // namespace engine::net
