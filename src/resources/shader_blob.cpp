#include <engine/resources/shader_blob.h>

#include <limits>
#include <span>
#include <string_view>

namespace engine {
namespace {

constexpr std::uint8_t kMagic[4] = {'W', 'S', 'H', 'D'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxBytes = 4u * 1024u * 1024u;

class Writer {
public:
    void u32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
    }

    void bytes(std::string_view text) {
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }

    void words(std::span<const std::uint32_t> values) {
        for (const std::uint32_t word : values) {
            u32(word);
        }
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    std::uint32_t u32() {
        if (cursor_ + 4 > bytes_.size()) {
            ok_ = false;
            return 0;
        }
        const std::uint32_t value = static_cast<std::uint32_t>(bytes_[cursor_])
                | (static_cast<std::uint32_t>(bytes_[cursor_ + 1]) << 8)
                | (static_cast<std::uint32_t>(bytes_[cursor_ + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes_[cursor_ + 3]) << 24);
        cursor_ += 4;
        return value;
    }

    std::string text(std::uint32_t size) {
        if (size > kMaxBytes || cursor_ + size > bytes_.size()) {
            ok_ = false;
            return {};
        }
        std::string out(reinterpret_cast<const char*>(bytes_.data() + cursor_), size);
        cursor_ += size;
        return out;
    }

    std::vector<std::uint32_t> words(std::uint32_t count) {
        if (count > kMaxBytes / 4u) {
            ok_ = false;
            return {};
        }
        std::vector<std::uint32_t> out;
        out.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            out.push_back(u32());
        }
        return out;
    }

    [[nodiscard]] bool finished() const noexcept { return ok_ && cursor_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
    bool ok_ = true;
};

}

std::vector<std::uint8_t> encode_shader_blob(const render::ShaderDesc& desc) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(kMagic[0]) | (static_cast<std::uint32_t>(kMagic[1]) << 8)
            | (static_cast<std::uint32_t>(kMagic[2]) << 16) | (static_cast<std::uint32_t>(kMagic[3]) << 24));
    writer.u32(kVersion);
    writer.u32(static_cast<std::uint32_t>(desc.vertex_src.size()));
    writer.bytes(desc.vertex_src);
    writer.u32(static_cast<std::uint32_t>(desc.fragment_src.size()));
    writer.bytes(desc.fragment_src);
    writer.u32(static_cast<std::uint32_t>(desc.vertex_spirv.size()));
    writer.words(desc.vertex_spirv);
    writer.u32(static_cast<std::uint32_t>(desc.fragment_spirv.size()));
    writer.words(desc.fragment_spirv);
    return writer.take();
}

std::expected<render::ShaderDesc, std::string> decode_shader_blob(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t expect = static_cast<std::uint32_t>(kMagic[0]) | (static_cast<std::uint32_t>(kMagic[1]) << 8)
            | (static_cast<std::uint32_t>(kMagic[2]) << 16) | (static_cast<std::uint32_t>(kMagic[3]) << 24);
    if (!reader.ok() || magic != expect) {
        return std::unexpected("not a shader blob");
    }
    if (reader.u32() != kVersion) {
        return std::unexpected("unsupported shader blob version");
    }
    render::ShaderDesc desc;
    const std::uint32_t vs_len = reader.u32();
    desc.vertex_src = reader.text(vs_len);
    const std::uint32_t fs_len = reader.u32();
    desc.fragment_src = reader.text(fs_len);
    const std::uint32_t vs_words = reader.u32();
    desc.vertex_spirv = reader.words(vs_words);
    const std::uint32_t fs_words = reader.u32();
    desc.fragment_spirv = reader.words(fs_words);
    if (!reader.finished() || desc.vertex_src.empty() || desc.fragment_src.empty()) {
        return std::unexpected("truncated shader blob");
    }
    if (desc.vertex_src.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())
            || desc.fragment_src.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::unexpected("truncated shader blob");
    }
    return desc;
}

}
