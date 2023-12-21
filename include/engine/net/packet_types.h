#pragma once

#include <engine/net/byte_stream.h>
#include <engine/net/net_id.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::net {

enum class PacketType : std::uint8_t {
    Unknown = 0,
    Handshake = 1,
    HandshakeAck = 2,
    Ping = 3,
    Pong = 4,
    Disconnect = 5,

    SpawnEntity = 10,
    DespawnEntity = 11,
    SyncTransform = 12,
    SyncParent = 13,

    ClientInput = 20,
    Custom = 100,
};

struct PacketHeader {
    PacketType type{PacketType::Unknown};

    constexpr bool operator==(const PacketHeader&) const noexcept = default;
};

inline void write_packet_header(ByteWriter& writer, PacketType type) {
    writer.write_enum(type);
}

[[nodiscard]] inline std::optional<PacketHeader> read_packet_header(ByteReader& reader) {
    const auto type = reader.read_enum<PacketType>();
    if (!type.has_value()) {
        return std::nullopt;
    }
    return PacketHeader{.type = *type};
}

struct SpawnEntityPacket {
    NetId net_id{kInvalidNetId};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    NetId parent_net_id{kInvalidNetId};

    constexpr bool operator==(const SpawnEntityPacket&) const noexcept = default;

    void serialize(ByteWriter& writer) const {
        writer.write_net_id(net_id);
        writer.write_vec3(position);
        writer.write_quat(rotation);
        writer.write_vec3(scale);
        writer.write_net_id(parent_net_id);
    }

    [[nodiscard]] static std::optional<SpawnEntityPacket> deserialize(ByteReader& reader) {
        SpawnEntityPacket p;
        if (!reader.read(p.net_id) || !reader.read(p.position) || !reader.read(p.rotation) ||
            !reader.read(p.scale) || !reader.read(p.parent_net_id)) {
            return std::nullopt;
        }
        return p;
    }
};

struct DespawnEntityPacket {
    NetId net_id{kInvalidNetId};

    constexpr bool operator==(const DespawnEntityPacket&) const noexcept = default;

    void serialize(ByteWriter& writer) const {
        writer.write_net_id(net_id);
    }

    [[nodiscard]] static std::optional<DespawnEntityPacket> deserialize(ByteReader& reader) {
        DespawnEntityPacket p;
        if (!reader.read(p.net_id)) {
            return std::nullopt;
        }
        return p;
    }
};

struct SyncTransformPacket {
    NetId net_id{kInvalidNetId};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    constexpr bool operator==(const SyncTransformPacket&) const noexcept = default;

    void serialize(ByteWriter& writer) const {
        writer.write_net_id(net_id);
        writer.write_vec3(position);
        writer.write_quat(rotation);
        writer.write_vec3(scale);
    }

    [[nodiscard]] static std::optional<SyncTransformPacket> deserialize(ByteReader& reader) {
        SyncTransformPacket p;
        if (!reader.read(p.net_id) || !reader.read(p.position) || !reader.read(p.rotation) ||
            !reader.read(p.scale)) {
            return std::nullopt;
        }
        return p;
    }
};

struct SyncParentPacket {
    NetId child_net_id{kInvalidNetId};
    NetId parent_net_id{kInvalidNetId};

    constexpr bool operator==(const SyncParentPacket&) const noexcept = default;

    void serialize(ByteWriter& writer) const {
        writer.write_net_id(child_net_id);
        writer.write_net_id(parent_net_id);
    }

    [[nodiscard]] static std::optional<SyncParentPacket> deserialize(ByteReader& reader) {
        SyncParentPacket p;
        if (!reader.read(p.child_net_id) || !reader.read(p.parent_net_id)) {
            return std::nullopt;
        }
        return p;
    }
};

struct HandshakePacket {
    std::uint32_t protocol_version{1};
    std::string client_name{};

    bool operator==(const HandshakePacket&) const noexcept = default;

    void serialize(ByteWriter& writer) const {
        writer.write_u32(protocol_version);
        writer.write_string(client_name);
    }

    [[nodiscard]] static std::optional<HandshakePacket> deserialize(ByteReader& reader) {
        HandshakePacket p;
        if (!reader.read(p.protocol_version) || !reader.read(p.client_name)) {
            return std::nullopt;
        }
        return p;
    }
};

} // namespace engine::net
