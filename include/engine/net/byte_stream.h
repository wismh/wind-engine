#pragma once

#include <engine/net/net_id.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine::net {

class ByteWriter {
public:
    ByteWriter() = default;
    explicit ByteWriter(std::size_t reserve_bytes);

    void write_u8(std::uint8_t val);
    void write_i8(std::int8_t val);
    void write_u16(std::uint16_t val);
    void write_i16(std::int16_t val);
    void write_u32(std::uint32_t val);
    void write_i32(std::int32_t val);
    void write_u64(std::uint64_t val);
    void write_i64(std::int64_t val);
    void write_float(float val);
    void write_double(double val);
    void write_bool(bool val);

    void write_vec2(const glm::vec2& val);
    void write_vec3(const glm::vec3& val);
    void write_vec4(const glm::vec4& val);
    void write_quat(const glm::quat& val);
    void write_net_id(NetId id);

    void write_string(std::string_view val);
    // u32 length, then the bytes.
    void write_bytes(std::span<const std::uint8_t> bytes);
    // The bytes as they are, with no length.
    void append(std::span<const std::uint8_t> bytes);

    template <typename EnumT>
        requires std::is_enum_v<EnumT>
    void write_enum(EnumT val) {
        using Underlying = std::underlying_type_t<EnumT>;
        write<Underlying>(static_cast<Underlying>(val));
    }

    template <typename T>
    void write(const T& val) {
        if constexpr (std::is_same_v<T, std::uint8_t>) {
            write_u8(val);
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            write_u16(val);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            write_u32(val);
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
            write_u64(val);
        } else if constexpr (std::is_same_v<T, std::int8_t>) {
            write_i8(val);
        } else if constexpr (std::is_same_v<T, std::int16_t>) {
            write_i16(val);
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            write_i32(val);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            write_i64(val);
        } else if constexpr (std::is_same_v<T, float>) {
            write_float(val);
        } else if constexpr (std::is_same_v<T, double>) {
            write_double(val);
        } else if constexpr (std::is_same_v<T, bool>) {
            write_bool(val);
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
            write_vec2(val);
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            write_vec3(val);
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            write_vec4(val);
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            write_quat(val);
        } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
            write_string(val);
        } else if constexpr (std::is_enum_v<T>) {
            write_enum(val);
        } else {
            static_assert(!sizeof(T*), "Unsupported type for ByteWriter::write");
        }
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> span() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>& buffer() const noexcept;
    [[nodiscard]] const std::uint8_t* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void clear() noexcept;
    void reserve(std::size_t capacity);

private:
    template <typename T>
    void write_raw_le(T val) {
        if constexpr (std::endian::native == std::endian::big) {
            val = std::byteswap(val);
        }
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&val);
        buffer_.insert(buffer_.end(), raw, raw + sizeof(T));
    }

    std::vector<std::uint8_t> buffer_{};
};

class ByteReader {
public:
    ByteReader() = default;
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept;

    [[nodiscard]] std::optional<std::uint8_t> read_u8();
    [[nodiscard]] std::optional<std::int8_t> read_i8();
    [[nodiscard]] std::optional<std::uint16_t> read_u16();
    [[nodiscard]] std::optional<std::int16_t> read_i16();
    [[nodiscard]] std::optional<std::uint32_t> read_u32();
    [[nodiscard]] std::optional<std::int32_t> read_i32();
    [[nodiscard]] std::optional<std::uint64_t> read_u64();
    [[nodiscard]] std::optional<std::int64_t> read_i64();
    [[nodiscard]] std::optional<float> read_float();
    [[nodiscard]] std::optional<double> read_double();
    [[nodiscard]] std::optional<bool> read_bool();

    [[nodiscard]] std::optional<glm::vec2> read_vec2();
    [[nodiscard]] std::optional<glm::vec3> read_vec3();
    [[nodiscard]] std::optional<glm::vec4> read_vec4();
    [[nodiscard]] std::optional<glm::quat> read_quat();
    [[nodiscard]] std::optional<NetId> read_net_id();

    [[nodiscard]] std::optional<std::string> read_string();
    [[nodiscard]] std::optional<std::span<const std::uint8_t>> read_bytes(std::size_t count);
    bool read_bytes(std::span<std::uint8_t> out);

    template <typename EnumT>
        requires std::is_enum_v<EnumT>
    [[nodiscard]] std::optional<EnumT> read_enum() {
        using Underlying = std::underlying_type_t<EnumT>;
        const auto val = read<Underlying>();
        if (!val) {
            return std::nullopt;
        }
        return static_cast<EnumT>(*val);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> read() {
        if constexpr (std::is_same_v<T, std::uint8_t>) {
            return read_u8();
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            return read_u16();
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            return read_u32();
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
            return read_u64();
        } else if constexpr (std::is_same_v<T, std::int8_t>) {
            return read_i8();
        } else if constexpr (std::is_same_v<T, std::int16_t>) {
            return read_i16();
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            return read_i32();
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return read_i64();
        } else if constexpr (std::is_same_v<T, float>) {
            return read_float();
        } else if constexpr (std::is_same_v<T, double>) {
            return read_double();
        } else if constexpr (std::is_same_v<T, bool>) {
            return read_bool();
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
            return read_vec2();
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            return read_vec3();
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            return read_vec4();
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            return read_quat();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return read_string();
        } else if constexpr (std::is_enum_v<T>) {
            return read_enum<T>();
        } else {
            static_assert(!sizeof(T*), "Unsupported type for ByteReader::read");
        }
    }

    template <typename T>
    bool read(T& out) {
        auto opt = read<T>();
        if (!opt) {
            return false;
        }
        out = *opt;
        return true;
    }

    [[nodiscard]] bool is_ok() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] bool has_remaining(std::size_t count = 1) const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept {
        return is_ok();
    }

    void reset(std::span<const std::uint8_t> data) noexcept;
    bool skip(std::size_t count) noexcept;

private:
    template <typename T>
    std::optional<T> read_raw_le() {
        if (!is_ok_ || offset_ + sizeof(T) > data_.size()) {
            is_ok_ = false;
            return std::nullopt;
        }
        T val{};
        std::memcpy(&val, data_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        if constexpr (std::endian::native == std::endian::big) {
            val = std::byteswap(val);
        }
        return val;
    }

    std::span<const std::uint8_t> data_{};
    std::size_t offset_{0};
    bool is_ok_{true};
};

} // namespace engine::net
