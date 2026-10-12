#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    void write_bool(bool val);
    void write_vec2(const glm::vec2& val);
    void write_vec3(const glm::vec3& val);
    void write_string(std::string_view val);
    void write_bytes(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const;
    [[nodiscard]] const std::vector<std::uint8_t>& buffer() const;
    [[nodiscard]] std::size_t size() const;
    void clear();

private:
    template <typename T>
    void write_pod(const T& pod) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&pod);
        buffer_.insert(buffer_.end(), raw, raw + sizeof(T));
    }

    std::vector<std::uint8_t> buffer_{};
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data);

    std::optional<std::uint8_t> read_u8();
    std::optional<std::int8_t> read_i8();
    std::optional<std::uint16_t> read_u16();
    std::optional<std::int16_t> read_i16();
    std::optional<std::uint32_t> read_u32();
    std::optional<std::int32_t> read_i32();
    std::optional<std::uint64_t> read_u64();
    std::optional<std::int64_t> read_i64();
    std::optional<float> read_float();
    std::optional<bool> read_bool();
    std::optional<glm::vec2> read_vec2();
    std::optional<glm::vec3> read_vec3();
    std::optional<std::string> read_string();
    std::optional<std::span<const std::uint8_t>> read_bytes(std::size_t count);

    [[nodiscard]] bool is_ok() const;
    [[nodiscard]] std::size_t remaining() const;
    [[nodiscard]] std::size_t offset() const;

private:
    template <typename T>
    std::optional<T> read_pod() {
        if (offset_ + sizeof(T) > data_.size()) {
            has_error_ = true;
            return std::nullopt;
        }
        T pod{};
        std::memcpy(&pod, data_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return pod;
    }

    std::span<const std::uint8_t> data_{};
    std::size_t offset_{0};
    bool has_error_{false};
};

} // namespace engine::net
