#include "net/input_message.h"

#include <engine/net/net_messages.h>

namespace engine::net {

std::vector<std::uint8_t> InputMessage::encode() const {
    ByteWriter out;
    out.write_u16(kInputMessageId);
    out.write_u32(newest);
    out.write_u8(static_cast<std::uint8_t>(entities.size()));
    for (const Entity& entity : entities) {
        out.write_u32(entity.id);
        out.write_u8(static_cast<std::uint8_t>(entity.inputs.size()));
        for (const auto& input : entity.inputs) {
            out.write_u16(static_cast<std::uint16_t>(input.size()));
            out.append(input);
        }
    }
    return out.buffer();
}

std::optional<InputMessage> InputMessage::decode(ByteReader& reader) {
    InputMessage message;
    const auto newest = reader.read_u32();
    const auto count = reader.read_u8();
    if (!newest || !count || *newest == kNoTick || *count == 0 || *count > kMaxInputEntities) {
        return std::nullopt;
    }
    message.newest = *newest;
    message.entities.reserve(*count);
    for (std::uint8_t e = 0; e < *count; ++e) {
        const auto id = reader.read_u32();
        const auto inputs = reader.read_u8();
        if (!id || !inputs || *id == kInvalidNetId || *inputs == 0 || *inputs > kMaxInputsPerMessage ||
                *inputs > *newest) {
            return std::nullopt;
        }
        Entity entity{.id = *id};
        entity.inputs.reserve(*inputs);
        for (std::uint8_t i = 0; i < *inputs; ++i) {
            const auto size = reader.read_u16();
            if (!size || *size > kMaxInputBytes) {
                return std::nullopt;
            }
            const auto bytes = reader.read_bytes(*size);
            if (!bytes) {
                return std::nullopt;
            }
            entity.inputs.push_back(*bytes);
        }
        message.entities.push_back(std::move(entity));
    }
    if (!reader.is_ok() || reader.remaining() != 0) {
        return std::nullopt;
    }
    return message;
}

} // namespace engine::net
