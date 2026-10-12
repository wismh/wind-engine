#include <engine/net/byte_stream.h>

namespace engine::net {

ByteWriter::ByteWriter(std::size_t reserve_bytes) {
    buffer_.reserve(reserve_bytes);
}

void ByteWriter::write_u8(std::uint8_t val) {
    write_pod(val);
}

void ByteWriter::write_i8(std::int8_t val) {
    write_pod(val);
}

void ByteWriter::write_u16(std::uint16_t val) {
    write_pod(val);
}

void ByteWriter::write_i16(std::int16_t val) {
    write_pod(val);
}

void ByteWriter::write_u32(std::uint32_t val) {
    write_pod(val);
}

void ByteWriter::write_i32(std::int32_t val) {
    write_pod(val);
}

void ByteWriter::write_u64(std::uint64_t val) {
    write_pod(val);
}

void ByteWriter::write_i64(std::int64_t val) {
    write_pod(val);
}

void ByteWriter::write_float(float val) {
    write_pod(val);
}

void ByteWriter::write_bool(bool val) {
    write_u8(val ? 1 : 0);
}

void ByteWriter::write_vec2(const glm::vec2& val) {
    write_float(val.x);
    write_float(val.y);
}

void ByteWriter::write_vec3(const glm::vec3& val) {
    write_float(val.x);
    write_float(val.y);
    write_float(val.z);
}

void ByteWriter::write_string(std::string_view val) {
    const auto len = static_cast<std::uint32_t>(val.size());
    write_u32(len);
    const auto* raw = reinterpret_cast<const std::uint8_t*>(val.data());
    buffer_.insert(buffer_.end(), raw, raw + len);
}

void ByteWriter::write_bytes(std::span<const std::uint8_t> bytes) {
    const auto len = static_cast<std::uint32_t>(bytes.size());
    write_u32(len);
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

std::span<const std::uint8_t> ByteWriter::bytes() const {
    return buffer_;
}

const std::vector<std::uint8_t>& ByteWriter::buffer() const {
    return buffer_;
}

std::size_t ByteWriter::size() const {
    return buffer_.size();
}

void ByteWriter::clear() {
    buffer_.clear();
}

ByteReader::ByteReader(std::span<const std::uint8_t> data)
    : data_(data) {}

std::optional<std::uint8_t> ByteReader::read_u8() {
    return read_pod<std::uint8_t>();
}

std::optional<std::int8_t> ByteReader::read_i8() {
    return read_pod<std::int8_t>();
}

std::optional<std::uint16_t> ByteReader::read_u16() {
    return read_pod<std::uint16_t>();
}

std::optional<std::int16_t> ByteReader::read_i16() {
    return read_pod<std::int16_t>();
}

std::optional<std::uint32_t> ByteReader::read_u32() {
    return read_pod<std::uint32_t>();
}

std::optional<std::int32_t> ByteReader::read_i32() {
    return read_pod<std::int32_t>();
}

std::optional<std::uint64_t> ByteReader::read_u64() {
    return read_pod<std::uint64_t>();
}

std::optional<std::int64_t> ByteReader::read_i64() {
    return read_pod<std::int64_t>();
}

std::optional<float> ByteReader::read_float() {
    return read_pod<float>();
}

std::optional<bool> ByteReader::read_bool() {
    const auto u = read_u8();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return *u != 0;
}

std::optional<glm::vec2> ByteReader::read_vec2() {
    const auto x = read_float();
    const auto y = read_float();
    if (!x.has_value() || !y.has_value()) {
        return std::nullopt;
    }
    return glm::vec2(*x, *y);
}

std::optional<glm::vec3> ByteReader::read_vec3() {
    const auto x = read_float();
    const auto y = read_float();
    const auto z = read_float();
    if (!x.has_value() || !y.has_value() || !z.has_value()) {
        return std::nullopt;
    }
    return glm::vec3(*x, *y, *z);
}

std::optional<std::string> ByteReader::read_string() {
    const auto len = read_u32();
    if (!len.has_value()) {
        return std::nullopt;
    }
    if (offset_ + *len > data_.size()) {
        has_error_ = true;
        return std::nullopt;
    }
    std::string str(reinterpret_cast<const char*>(data_.data() + offset_), *len);
    offset_ += *len;
    return str;
}

std::optional<std::span<const std::uint8_t>> ByteReader::read_bytes(std::size_t count) {
    if (offset_ + count > data_.size()) {
        has_error_ = true;
        return std::nullopt;
    }
    std::span<const std::uint8_t> result(data_.data() + offset_, count);
    offset_ += count;
    return result;
}

bool ByteReader::is_ok() const {
    return !has_error_;
}

std::size_t ByteReader::remaining() const {
    if (offset_ >= data_.size()) {
        return 0;
    }
    return data_.size() - offset_;
}

std::size_t ByteReader::offset() const {
    return offset_;
}

} // namespace engine::net
