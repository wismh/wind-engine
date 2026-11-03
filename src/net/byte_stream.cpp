#include <engine/net/byte_stream.h>

namespace engine::net {

ByteWriter::ByteWriter(std::size_t reserve_bytes) {
    buffer_.reserve(reserve_bytes);
}

void ByteWriter::write_u8(std::uint8_t val) {
    buffer_.push_back(val);
}

void ByteWriter::write_i8(std::int8_t val) {
    buffer_.push_back(static_cast<std::uint8_t>(val));
}

void ByteWriter::write_u16(std::uint16_t val) {
    write_raw_le(val);
}

void ByteWriter::write_i16(std::int16_t val) {
    write_raw_le(static_cast<std::uint16_t>(val));
}

void ByteWriter::write_u32(std::uint32_t val) {
    write_raw_le(val);
}

void ByteWriter::write_i32(std::int32_t val) {
    write_raw_le(static_cast<std::uint32_t>(val));
}

void ByteWriter::write_u64(std::uint64_t val) {
    write_raw_le(val);
}

void ByteWriter::write_i64(std::int64_t val) {
    write_raw_le(static_cast<std::uint64_t>(val));
}

void ByteWriter::write_float(float val) {
    write_raw_le(std::bit_cast<std::uint32_t>(val));
}

void ByteWriter::write_double(double val) {
    write_raw_le(std::bit_cast<std::uint64_t>(val));
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

void ByteWriter::write_vec4(const glm::vec4& val) {
    write_float(val.x);
    write_float(val.y);
    write_float(val.z);
    write_float(val.w);
}

void ByteWriter::write_quat(const glm::quat& val) {
    write_float(val.w);
    write_float(val.x);
    write_float(val.y);
    write_float(val.z);
}

void ByteWriter::write_net_id(NetId id) {
    write_u32(id);
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

void ByteWriter::append(std::span<const std::uint8_t> bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

std::span<const std::uint8_t> ByteWriter::bytes() const noexcept {
    return buffer_;
}

std::span<const std::uint8_t> ByteWriter::span() const noexcept {
    return buffer_;
}

const std::vector<std::uint8_t>& ByteWriter::buffer() const noexcept {
    return buffer_;
}

const std::uint8_t* ByteWriter::data() const noexcept {
    return buffer_.data();
}

std::size_t ByteWriter::size() const noexcept {
    return buffer_.size();
}

bool ByteWriter::empty() const noexcept {
    return buffer_.empty();
}

void ByteWriter::clear() noexcept {
    buffer_.clear();
}

void ByteWriter::reserve(std::size_t capacity) {
    buffer_.reserve(capacity);
}

ByteReader::ByteReader(std::span<const std::uint8_t> data) noexcept
    : data_(data), offset_(0), is_ok_(true) {}

std::optional<std::uint8_t> ByteReader::read_u8() {
    return read_raw_le<std::uint8_t>();
}

std::optional<std::int8_t> ByteReader::read_i8() {
    const auto u = read_u8();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int8_t>(*u);
}

std::optional<std::uint16_t> ByteReader::read_u16() {
    return read_raw_le<std::uint16_t>();
}

std::optional<std::int16_t> ByteReader::read_i16() {
    const auto u = read_u16();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(*u);
}

std::optional<std::uint32_t> ByteReader::read_u32() {
    return read_raw_le<std::uint32_t>();
}

std::optional<std::int32_t> ByteReader::read_i32() {
    const auto u = read_u32();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*u);
}

std::optional<std::uint64_t> ByteReader::read_u64() {
    return read_raw_le<std::uint64_t>();
}

std::optional<std::int64_t> ByteReader::read_i64() {
    const auto u = read_u64();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*u);
}

std::optional<float> ByteReader::read_float() {
    const auto u = read_raw_le<std::uint32_t>();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return std::bit_cast<float>(*u);
}

std::optional<double> ByteReader::read_double() {
    const auto u = read_raw_le<std::uint64_t>();
    if (!u.has_value()) {
        return std::nullopt;
    }
    return std::bit_cast<double>(*u);
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

std::optional<glm::vec4> ByteReader::read_vec4() {
    const auto x = read_float();
    const auto y = read_float();
    const auto z = read_float();
    const auto w = read_float();
    if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
        return std::nullopt;
    }
    return glm::vec4(*x, *y, *z, *w);
}

std::optional<glm::quat> ByteReader::read_quat() {
    const auto w = read_float();
    const auto x = read_float();
    const auto y = read_float();
    const auto z = read_float();
    if (!w.has_value() || !x.has_value() || !y.has_value() || !z.has_value()) {
        return std::nullopt;
    }
    return glm::quat(*w, *x, *y, *z);
}

std::optional<NetId> ByteReader::read_net_id() {
    return read_u32();
}

std::optional<std::string> ByteReader::read_string() {
    const auto len = read_u32();
    if (!len.has_value()) {
        return std::nullopt;
    }
    if (offset_ + *len > data_.size()) {
        is_ok_ = false;
        return std::nullopt;
    }
    std::string str(reinterpret_cast<const char*>(data_.data() + offset_), *len);
    offset_ += *len;
    return str;
}

std::optional<std::span<const std::uint8_t>> ByteReader::read_bytes(std::size_t count) {
    if (offset_ + count > data_.size()) {
        is_ok_ = false;
        return std::nullopt;
    }
    std::span<const std::uint8_t> result(data_.data() + offset_, count);
    offset_ += count;
    return result;
}

bool ByteReader::read_bytes(std::span<std::uint8_t> out) {
    if (!has_remaining(out.size())) {
        is_ok_ = false;
        return false;
    }
    std::memcpy(out.data(), data_.data() + offset_, out.size());
    offset_ += out.size();
    return true;
}

bool ByteReader::is_ok() const noexcept {
    return is_ok_;
}

std::size_t ByteReader::remaining() const noexcept {
    if (offset_ >= data_.size()) {
        return 0;
    }
    return data_.size() - offset_;
}

std::size_t ByteReader::offset() const noexcept {
    return offset_;
}

bool ByteReader::has_remaining(std::size_t count) const noexcept {
    return is_ok_ && (remaining() >= count);
}

void ByteReader::reset(std::span<const std::uint8_t> data) noexcept {
    data_ = data;
    offset_ = 0;
    is_ok_ = true;
}

bool ByteReader::skip(std::size_t count) noexcept {
    if (!has_remaining(count)) {
        is_ok_ = false;
        return false;
    }
    offset_ += count;
    return true;
}

} // namespace engine::net
