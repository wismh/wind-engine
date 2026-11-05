#include <engine/render/model.h>

#include <cstring>
#include <string>

namespace engine::render {
namespace {

constexpr std::uint8_t kMagic[4] = {'W', 'M', 'O', 'D'};
constexpr std::uint32_t kVersion = 1;

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
    }

    void i32(int value) { u32(static_cast<std::uint32_t>(value)); }

    void f32(float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void str(std::string_view text) {
        u32(static_cast<std::uint32_t>(text.size()));
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }

    void vec3(const glm::vec3& value) {
        f32(value.x);
        f32(value.y);
        f32(value.z);
    }

    void quat(const glm::quat& value) {
        f32(value.x);
        f32(value.y);
        f32(value.z);
        f32(value.w);
    }

    void mat4(const glm::mat4& value) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                f32(value[column][row]);
            }
        }
    }

    void vec4(const glm::vec4& value) {
        f32(value.x);
        f32(value.y);
        f32(value.z);
        f32(value.w);
    }

    [[nodiscard]] std::vector<std::uint8_t>& bytes() { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool ok() const { return ok_; }

    bool u8(std::uint8_t& value) {
        if (cursor_ >= bytes_.size()) {
            ok_ = false;
            return false;
        }
        value = bytes_[cursor_++];
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (cursor_ + 4 > bytes_.size()) {
            ok_ = false;
            return false;
        }
        value = static_cast<std::uint32_t>(bytes_[cursor_]) |
                (static_cast<std::uint32_t>(bytes_[cursor_ + 1]) << 8) |
                (static_cast<std::uint32_t>(bytes_[cursor_ + 2]) << 16) |
                (static_cast<std::uint32_t>(bytes_[cursor_ + 3]) << 24);
        cursor_ += 4;
        return true;
    }

    bool i32(int& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) {
            return false;
        }
        value = static_cast<int>(bits);
        return true;
    }

    bool f32(float& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool str(std::string& value) {
        std::uint32_t length = 0;
        if (!u32(length) || cursor_ + length > bytes_.size()) {
            ok_ = false;
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + cursor_), length);
        cursor_ += length;
        return true;
    }

    bool vec3(glm::vec3& value) { return f32(value.x) && f32(value.y) && f32(value.z); }

    bool quat(glm::quat& value) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
        if (!f32(x) || !f32(y) || !f32(z) || !f32(w)) {
            return false;
        }
        value = glm::quat(w, x, y, z);
        return true;
    }

    bool mat4(glm::mat4& value) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!f32(value[column][row])) {
                    return false;
                }
            }
        }
        return true;
    }

    bool vec4(glm::vec4& value) { return f32(value.x) && f32(value.y) && f32(value.z) && f32(value.w); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
    bool ok_ = true;
};

bool read_vertex(Reader& reader, MeshVertex& vertex) {
    return reader.vec3(vertex.position) && reader.vec3(vertex.normal) && reader.f32(vertex.uv.x) &&
           reader.f32(vertex.uv.y) && reader.vec4(vertex.color) && reader.f32(vertex.layer);
}

}

std::vector<std::uint8_t> serialize_model_blob(const ModelAsset& model) {
    Writer writer;
    writer.u8(kMagic[0]);
    writer.u8(kMagic[1]);
    writer.u8(kMagic[2]);
    writer.u8(kMagic[3]);
    writer.u32(kVersion);

    writer.u32(static_cast<std::uint32_t>(model.nodes.size()));
    for (const ModelNode& node : model.nodes) {
        writer.str(node.name);
        writer.i32(node.parent);
        writer.vec3(node.translation);
        writer.quat(node.rotation);
        writer.vec3(node.scale);
    }

    writer.u32(static_cast<std::uint32_t>(model.skins.size()));
    for (const ModelSkin& skin : model.skins) {
        writer.u32(static_cast<std::uint32_t>(skin.joints.size()));
        for (const int joint : skin.joints) {
            writer.i32(joint);
        }
        for (const glm::mat4& inverse_bind : skin.inverse_bind) {
            writer.mat4(inverse_bind);
        }
    }

    writer.u32(static_cast<std::uint32_t>(model.primitives.size()));
    for (const ModelPrimitive& primitive : model.primitives) {
        writer.i32(primitive.node);
        writer.i32(primitive.skin);
        writer.i32(primitive.material);
        const MeshDesc& mesh = primitive.mesh;
        writer.u32(static_cast<std::uint32_t>(mesh.vertices.size()));
        for (const MeshVertex& vertex : mesh.vertices) {
            writer.vec3(vertex.position);
            writer.vec3(vertex.normal);
            writer.f32(vertex.uv.x);
            writer.f32(vertex.uv.y);
            writer.vec4(vertex.color);
            writer.f32(vertex.layer);
        }
        writer.u32(static_cast<std::uint32_t>(mesh.indices.size()));
        for (const std::uint32_t index : mesh.indices) {
            writer.u32(index);
        }
        writer.u32(static_cast<std::uint32_t>(mesh.influences.size()));
        for (const SkinInfluence& influence : mesh.influences) {
            for (const std::uint8_t joint : influence.joints) {
                writer.u8(joint);
            }
            for (const float weight : influence.weights) {
                writer.f32(weight);
            }
        }
        writer.u8(mesh.bounds_override ? 1 : 0);
        if (mesh.bounds_override) {
            writer.vec3(mesh.bounds_override->min);
            writer.vec3(mesh.bounds_override->max);
        }
    }

    writer.u32(static_cast<std::uint32_t>(model.materials.size()));
    for (const ModelMaterial& material : model.materials) {
        writer.vec4(material.base_color);
        writer.u8(static_cast<std::uint8_t>(material.alpha));
        writer.f32(material.alpha_cutoff);
        writer.u8(material.double_sided ? 1 : 0);
        writer.u8(material.unlit ? 1 : 0);
        writer.i32(material.texture);
    }

    writer.u32(static_cast<std::uint32_t>(model.textures.size()));
    for (const ModelTexture& texture : model.textures) {
        writer.u32(static_cast<std::uint32_t>(texture.width));
        writer.u32(static_cast<std::uint32_t>(texture.height));
        writer.u32(static_cast<std::uint32_t>(texture.rgba.size()));
        writer.bytes().insert(writer.bytes().end(), texture.rgba.begin(), texture.rgba.end());
    }

    writer.u32(static_cast<std::uint32_t>(model.clips.size()));
    for (const ModelClip& clip : model.clips) {
        writer.str(clip.name);
        writer.f32(clip.duration);
        writer.u32(static_cast<std::uint32_t>(clip.key_count));
        writer.u32(static_cast<std::uint32_t>(clip.samples.size()));
        for (const ModelNodePose& sample : clip.samples) {
            writer.vec3(sample.translation);
            writer.quat(sample.rotation);
            writer.vec3(sample.scale);
        }
    }
    return std::move(writer.bytes());
}

std::optional<ModelAsset> parse_model_blob(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::uint8_t magic[4] = {};
    if (!reader.u8(magic[0]) || !reader.u8(magic[1]) || !reader.u8(magic[2]) || !reader.u8(magic[3]) ||
        magic[0] != kMagic[0] || magic[1] != kMagic[1] || magic[2] != kMagic[2] || magic[3] != kMagic[3]) {
        return std::nullopt;
    }
    std::uint32_t version = 0;
    if (!reader.u32(version) || version != kVersion) {
        return std::nullopt;
    }

    ModelAsset model;
    std::uint32_t node_count = 0;
    if (!reader.u32(node_count)) {
        return std::nullopt;
    }
    model.nodes.resize(node_count);
    for (ModelNode& node : model.nodes) {
        if (!reader.str(node.name) || !reader.i32(node.parent) || !reader.vec3(node.translation) ||
            !reader.quat(node.rotation) || !reader.vec3(node.scale)) {
            return std::nullopt;
        }
    }

    std::uint32_t skin_count = 0;
    if (!reader.u32(skin_count)) {
        return std::nullopt;
    }
    model.skins.resize(skin_count);
    for (ModelSkin& skin : model.skins) {
        std::uint32_t joint_count = 0;
        if (!reader.u32(joint_count)) {
            return std::nullopt;
        }
        skin.joints.resize(joint_count);
        for (int& joint : skin.joints) {
            if (!reader.i32(joint)) {
                return std::nullopt;
            }
        }
        skin.inverse_bind.resize(joint_count);
        for (glm::mat4& inverse_bind : skin.inverse_bind) {
            if (!reader.mat4(inverse_bind)) {
                return std::nullopt;
            }
        }
    }

    std::uint32_t primitive_count = 0;
    if (!reader.u32(primitive_count)) {
        return std::nullopt;
    }
    model.primitives.resize(primitive_count);
    for (ModelPrimitive& primitive : model.primitives) {
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
        std::uint32_t influence_count = 0;
        if (!reader.i32(primitive.node) || !reader.i32(primitive.skin) || !reader.i32(primitive.material) ||
            !reader.u32(vertex_count)) {
            return std::nullopt;
        }
        primitive.mesh.vertices.resize(vertex_count);
        for (MeshVertex& vertex : primitive.mesh.vertices) {
            if (!read_vertex(reader, vertex)) {
                return std::nullopt;
            }
        }
        if (!reader.u32(index_count)) {
            return std::nullopt;
        }
        primitive.mesh.indices.resize(index_count);
        for (std::uint32_t& index : primitive.mesh.indices) {
            if (!reader.u32(index)) {
                return std::nullopt;
            }
        }
        if (!reader.u32(influence_count)) {
            return std::nullopt;
        }
        primitive.mesh.influences.resize(influence_count);
        for (SkinInfluence& influence : primitive.mesh.influences) {
            for (std::uint8_t& joint : influence.joints) {
                if (!reader.u8(joint)) {
                    return std::nullopt;
                }
            }
            for (float& weight : influence.weights) {
                if (!reader.f32(weight)) {
                    return std::nullopt;
                }
            }
        }
        std::uint8_t has_bounds = 0;
        if (!reader.u8(has_bounds)) {
            return std::nullopt;
        }
        if (has_bounds != 0) {
            Aabb box;
            if (!reader.vec3(box.min) || !reader.vec3(box.max)) {
                return std::nullopt;
            }
            primitive.mesh.bounds_override = box;
        }
    }

    std::uint32_t material_count = 0;
    if (!reader.u32(material_count)) {
        return std::nullopt;
    }
    model.materials.resize(material_count);
    for (ModelMaterial& material : model.materials) {
        std::uint8_t alpha = 0;
        std::uint8_t double_sided = 0;
        std::uint8_t unlit = 0;
        if (!reader.vec4(material.base_color) || !reader.u8(alpha) || !reader.f32(material.alpha_cutoff) ||
            !reader.u8(double_sided) || !reader.u8(unlit) || !reader.i32(material.texture)) {
            return std::nullopt;
        }
        material.alpha = static_cast<ModelAlpha>(alpha);
        material.double_sided = double_sided != 0;
        material.unlit = unlit != 0;
    }

    std::uint32_t texture_count = 0;
    if (!reader.u32(texture_count)) {
        return std::nullopt;
    }
    model.textures.resize(texture_count);
    for (ModelTexture& texture : model.textures) {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t byte_count = 0;
        if (!reader.u32(width) || !reader.u32(height) || !reader.u32(byte_count)) {
            return std::nullopt;
        }
        texture.width = static_cast<int>(width);
        texture.height = static_cast<int>(height);
        texture.rgba.resize(byte_count);
        for (std::uint8_t& byte : texture.rgba) {
            if (!reader.u8(byte)) {
                return std::nullopt;
            }
        }
    }

    std::uint32_t clip_count = 0;
    if (!reader.u32(clip_count)) {
        return std::nullopt;
    }
    model.clips.resize(clip_count);
    for (ModelClip& clip : model.clips) {
        std::uint32_t key_count = 0;
        std::uint32_t sample_count = 0;
        if (!reader.str(clip.name) || !reader.f32(clip.duration) || !reader.u32(key_count) || !reader.u32(sample_count)) {
            return std::nullopt;
        }
        clip.key_count = static_cast<int>(key_count);
        clip.samples.resize(sample_count);
        for (ModelNodePose& sample : clip.samples) {
            if (!reader.vec3(sample.translation) || !reader.quat(sample.rotation) || !reader.vec3(sample.scale)) {
                return std::nullopt;
            }
        }
    }
    if (!reader.ok()) {
        return std::nullopt;
    }
    return model;
}

}
