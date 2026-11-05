#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "resources/stb_image.h"

#include "gltf/cook.h"

#include <engine/render/model.h>
#include <engine/resources/meta.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <fstream>
#include <unordered_map>

namespace engine::gltf {
namespace {

using render::kMaxSkinJoints;
using render::kModelBakeHz;
using render::ModelAsset;
using render::ModelClip;
using render::ModelMaterial;
using render::ModelNode;
using render::ModelNodePose;
using render::ModelPrimitive;
using render::ModelSkin;
using render::ModelTexture;

std::expected<std::vector<std::uint8_t>, CookError> fail(std::string message) {
    return std::unexpected(CookError{std::move(message)});
}

int node_index(const cgltf_data* data, const cgltf_node* node) {
    if (node == nullptr) {
        return -1;
    }
    return static_cast<int>(node - data->nodes);
}

bool read_floats(const cgltf_accessor* accessor, cgltf_size index, float* out, int count) {
    if (accessor == nullptr || accessor->is_sparse) {
        return false;
    }
    return cgltf_accessor_read_float(accessor, index, out, static_cast<cgltf_size>(count)) != 0;
}

bool read_index(const cgltf_accessor* accessor, cgltf_size index, std::uint32_t& out) {
    if (accessor == nullptr || accessor->is_sparse) {
        return false;
    }
    cgltf_uint value = 0;
    if (cgltf_accessor_read_uint(accessor, index, &value, 1) == 0) {
        return false;
    }
    out = value;
    return true;
}

const cgltf_accessor* attribute(const cgltf_primitive* primitive, cgltf_attribute_type type, int index) {
    for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
        if (primitive->attributes[i].type == type && primitive->attributes[i].index == index) {
            return primitive->attributes[i].data;
        }
    }
    return nullptr;
}

std::string decode_uri(std::string_view uri) {
    std::string out;
    out.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            const int hi = hex(uri[i + 1]);
            const int lo = hex(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(uri[i]);
    }
    return out;
}

std::expected<std::vector<std::uint8_t>, CookError> load_image(
        const cgltf_image* image, const std::filesystem::path& base_dir, int& width, int& height) {
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    std::vector<std::uint8_t> owned;
    if (image->buffer_view != nullptr && image->buffer_view->buffer != nullptr &&
            image->buffer_view->buffer->data != nullptr) {
        bytes = static_cast<const std::uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        size = image->buffer_view->size;
    } else if (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) != 0) {
        const auto relative = std::filesystem::path(decode_uri(image->uri)).lexically_normal();
        if (relative.empty() || *relative.begin() == "..") {
            return fail("an image uri escapes the model directory");
        }
        std::ifstream in(base_dir / relative, std::ios::binary);
        if (!in) {
            return fail(std::string("missing image ") + image->uri);
        }
        owned.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        bytes = owned.data();
        size = owned.size();
    } else {
        return fail("an image is neither a buffer view nor a relative file");
    }
    if (size >= 8) {
        const bool png = bytes[0] == 0x89 && bytes[1] == 'P';
        const bool jpeg = bytes[0] == 0xFF && bytes[1] == 0xD8;
        if (!png && !jpeg) {
            return fail("a model image is not png or jpeg");
        }
    }
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return fail("a model image failed to decode");
    }
    const std::size_t pixel_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> rgba(pixels, pixels + pixel_bytes);
    stbi_image_free(pixels);
    return rgba;
}

glm::quat sample_quat(const cgltf_animation_sampler* sampler, float time) {
    const cgltf_accessor* input = sampler->input;
    const cgltf_accessor* output = sampler->output;
    auto at = [&](cgltf_size index) {
        float value[4]{0, 0, 0, 1};
        read_floats(output, index, value, 4);
        return glm::quat(value[3], value[0], value[1], value[2]);
    };
    if (input == nullptr || output == nullptr || input->count == 0) {
        return glm::quat(1, 0, 0, 0);
    }
    if (input->count == 1 || sampler->interpolation == cgltf_interpolation_type_step) {
        cgltf_size index = 0;
        for (cgltf_size i = 0; i + 1 < input->count; ++i) {
            float key = 0;
            read_floats(input, i + 1, &key, 1);
            if (key <= time) {
                index = i + 1;
            }
        }
        if (sampler->interpolation != cgltf_interpolation_type_step && input->count > 1) {
            index = 0;
        } else {
            return glm::normalize(at(index));
        }
    }
    cgltf_size index = 0;
    for (cgltf_size i = 0; i + 1 < input->count; ++i) {
        float next = 0;
        read_floats(input, i + 1, &next, 1);
        if (next <= time) {
            index = i + 1;
        }
    }
    if (index + 1 >= input->count) {
        return glm::normalize(at(input->count - 1));
    }
    float t0 = 0;
    float t1 = 0;
    read_floats(input, index, &t0, 1);
    read_floats(input, index + 1, &t1, 1);
    const float span = t1 - t0;
    const float u = span > 0.0f ? std::clamp((time - t0) / span, 0.0f, 1.0f) : 0.0f;
    if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
        float left_out[4]{};
        float right_in[4]{};
        read_floats(output, index * 3 + 2, left_out, 4);
        read_floats(output, (index + 1) * 3, right_in, 4);
        const glm::quat p0 = at(index * 3 + 1);
        const glm::quat p1 = at((index + 1) * 3 + 1);
        const glm::quat m0(left_out[3] * span, left_out[0] * span, left_out[1] * span, left_out[2] * span);
        const glm::quat m1(right_in[3] * span, right_in[0] * span, right_in[1] * span, right_in[2] * span);
        const float u2 = u * u;
        const float u3 = u2 * u;
        glm::quat mixed = p0 * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + p1 * (-2 * u3 + 3 * u2) +
                          m1 * (u3 - u2);
        return glm::normalize(mixed);
    }
    return glm::normalize(glm::slerp(at(index), at(index + 1), u));
}

void sample_vec3(const cgltf_animation_sampler* sampler, float time, glm::vec3& out) {
    const cgltf_accessor* input = sampler->input;
    const cgltf_accessor* output = sampler->output;
    auto at = [&](cgltf_size index, glm::vec3& value) {
        float raw[3]{};
        read_floats(output, index, raw, 3);
        value = glm::vec3(raw[0], raw[1], raw[2]);
    };
    if (input == nullptr || output == nullptr || input->count == 0) {
        return;
    }
    cgltf_size index = 0;
    if (sampler->interpolation == cgltf_interpolation_type_step) {
        for (cgltf_size i = 0; i + 1 < input->count; ++i) {
            float key = 0;
            read_floats(input, i + 1, &key, 1);
            if (key <= time) {
                index = i + 1;
            }
        }
        at(index, out);
        return;
    }
    for (cgltf_size i = 0; i + 1 < input->count; ++i) {
        float next = 0;
        read_floats(input, i + 1, &next, 1);
        if (next <= time) {
            index = i + 1;
        }
    }
    if (index + 1 >= input->count) {
        cgltf_size last = input->count - 1;
        if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
            last = last * 3 + 1;
        }
        at(last, out);
        return;
    }
    float t0 = 0;
    float t1 = 0;
    read_floats(input, index, &t0, 1);
    read_floats(input, index + 1, &t1, 1);
    const float span = t1 - t0;
    const float u = span > 0.0f ? std::clamp((time - t0) / span, 0.0f, 1.0f) : 0.0f;
    cgltf_size i0 = index;
    cgltf_size i1 = index + 1;
    if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
        glm::vec3 p0;
        glm::vec3 p1;
        glm::vec3 m0;
        glm::vec3 m1;
        at(index * 3 + 1, p0);
        at((index + 1) * 3 + 1, p1);
        at(index * 3 + 2, m0);
        at((index + 1) * 3, m1);
        const float u2 = u * u;
        const float u3 = u2 * u;
        out = p0 * (2 * u3 - 3 * u2 + 1) + (m0 * span) * (u3 - 2 * u2 + u) + p1 * (-2 * u3 + 3 * u2) +
              (m1 * span) * (u3 - u2);
        return;
    }
    glm::vec3 a;
    glm::vec3 b;
    at(i0, a);
    at(i1, b);
    out = glm::mix(a, b, u);
}

std::expected<void, CookError> bake(const cgltf_data* data, ModelAsset& asset) {
    for (cgltf_size anim_index = 0; anim_index < data->animations_count; ++anim_index) {
        const cgltf_animation& anim = data->animations[anim_index];
        for (cgltf_size ch = 0; ch < anim.channels_count; ++ch) {
            if (anim.channels[ch].target_path == cgltf_animation_path_type_weights) {
                return std::unexpected(CookError{"morph weights are not supported"});
            }
        }
        float duration = 0.0f;
        for (cgltf_size s = 0; s < anim.samplers_count; ++s) {
            const cgltf_accessor* input = anim.samplers[s].input;
            if (input == nullptr || input->count == 0) {
                continue;
            }
            float end = 0.0f;
            if (!read_floats(input, input->count - 1, &end, 1)) {
                return std::unexpected(CookError{"an animation key is sparse or unreadable"});
            }
            duration = std::max(duration, end);
        }
        const float steps = std::ceil(duration * static_cast<float>(kModelBakeHz) - 1e-4f);
        const int keys = duration <= 0.0f ? 1 : 1 + static_cast<int>(steps);
        ModelClip clip;
        clip.name = anim.name != nullptr ? anim.name : "";
        if (clip.name.empty()) {
            clip.name = "clip";
        }
        for (const ModelClip& existing : asset.clips) {
            if (existing.name == clip.name) {
                clip.name += "_2";
                break;
            }
        }
        clip.duration = static_cast<float>(keys - 1) / static_cast<float>(kModelBakeHz);
        clip.key_count = keys;
        const int nodes = static_cast<int>(asset.nodes.size());
        clip.samples.assign(static_cast<std::size_t>(keys) * static_cast<std::size_t>(nodes), ModelNodePose{});
        for (int key = 0; key < keys; ++key) {
            const float time = std::min(static_cast<float>(key) / static_cast<float>(kModelBakeHz), duration);
            for (int node = 0; node < nodes; ++node) {
                ModelNodePose& pose = clip.samples[static_cast<std::size_t>(key) * nodes + node];
                pose.translation = asset.nodes[static_cast<std::size_t>(node)].translation;
                pose.rotation = asset.nodes[static_cast<std::size_t>(node)].rotation;
                pose.scale = asset.nodes[static_cast<std::size_t>(node)].scale;
            }
            for (cgltf_size ch = 0; ch < anim.channels_count; ++ch) {
                const cgltf_animation_channel& channel = anim.channels[ch];
                const int index = node_index(data, channel.target_node);
                if (index < 0 || channel.sampler == nullptr) {
                    continue;
                }
                ModelNodePose& pose = clip.samples[static_cast<std::size_t>(key) * nodes + index];
                if (channel.target_path == cgltf_animation_path_type_translation) {
                    sample_vec3(channel.sampler, time, pose.translation);
                } else if (channel.target_path == cgltf_animation_path_type_scale) {
                    sample_vec3(channel.sampler, time, pose.scale);
                } else if (channel.target_path == cgltf_animation_path_type_rotation) {
                    pose.rotation = sample_quat(channel.sampler, time);
                }
            }
        }
        asset.clips.push_back(std::move(clip));
    }
    return {};
}

void expand_bounds(ModelAsset& asset) {
    for (ModelPrimitive& primitive : asset.primitives) {
        if (primitive.skin < 0) {
            continue;
        }
        glm::vec3 min_v(std::numeric_limits<float>::max());
        glm::vec3 max_v(std::numeric_limits<float>::lowest());
        auto cover = [&](const std::vector<glm::mat4>& palette) {
            for (const render::MeshVertex& vertex : primitive.mesh.vertices) {
                const std::size_t index = static_cast<std::size_t>(&vertex - primitive.mesh.vertices.data());
                const render::SkinInfluence& influence = primitive.mesh.influences[index];
                glm::vec4 skinned(0);
                for (int i = 0; i < 4; ++i) {
                    if (influence.weights[i] == 0.0f) {
                        continue;
                    }
                    const int joint = influence.joints[i];
                    if (joint < 0 || joint >= static_cast<int>(palette.size())) {
                        continue;
                    }
                    const glm::vec4 bound = palette[static_cast<std::size_t>(joint)] * glm::vec4(vertex.position, 1);
                    skinned += influence.weights[i] * bound;
                }
                min_v = glm::min(min_v, glm::vec3(skinned));
                max_v = glm::max(max_v, glm::vec3(skinned));
            }
        };
        const auto rest = render::sample_model_locals(asset, "", 0.0f, false);
        const auto rest_global = render::model_globals(asset, rest);
        cover(render::model_skin_palette(asset, primitive.skin, primitive.node, rest_global));
        for (const ModelClip& clip : asset.clips) {
            for (int key = 0; key < clip.key_count; ++key) {
                const float time = clip.key_count <= 1
                        ? 0.0f
                        : clip.duration * (static_cast<float>(key) / static_cast<float>(clip.key_count - 1));
                const auto locals = render::sample_model_locals(asset, clip.name, time, false);
                const auto global = render::model_globals(asset, locals);
                cover(render::model_skin_palette(asset, primitive.skin, primitive.node, global));
            }
        }
        primitive.mesh.bounds_override = engine::render::Aabb{min_v, max_v};
    }
}

std::expected<ModelAsset, CookError> build(const cgltf_data* data, const std::filesystem::path& base_dir) {
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        if (std::strcmp(data->extensions_required[i], "KHR_materials_unlit") != 0) {
            return std::unexpected(
                    CookError{std::string("unsupported gltf extension ") + data->extensions_required[i]});
        }
    }
    ModelAsset asset;
    asset.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& node = data->nodes[i];
        if (node.weights_count > 0) {
            return std::unexpected(CookError{"morph targets are not supported"});
        }
        ModelNode& out = asset.nodes[i];
        out.name = node.name != nullptr ? node.name : "";
        out.parent = node_index(data, node.parent);
        if (node.has_matrix) {
            glm::mat4 matrix(1);
            std::memcpy(&matrix, node.matrix, sizeof(matrix));
            const engine::Transform decomposed = render::transform_from_matrix(matrix);
            out.translation = decomposed.position;
            out.rotation = decomposed.rotation;
            out.scale = decomposed.scale;
        } else {
            if (node.has_translation) {
                out.translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            }
            if (node.has_rotation) {
                out.rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
            }
            if (node.has_scale) {
                out.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            }
        }
    }

    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const cgltf_skin& skin = data->skins[i];
        if (skin.joints_count > kMaxSkinJoints) {
            return std::unexpected(CookError{"a skin has more than 128 joints"});
        }
        ModelSkin out;
        out.joints.resize(skin.joints_count);
        out.inverse_bind.resize(skin.joints_count, glm::mat4(1));
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            out.joints[j] = node_index(data, skin.joints[j]);
            if (skin.inverse_bind_matrices != nullptr) {
                if (skin.inverse_bind_matrices->is_sparse) {
                    return std::unexpected(CookError{"sparse accessors are not supported"});
                }
                float raw[16]{};
                if (!read_floats(skin.inverse_bind_matrices, j, raw, 16)) {
                    return std::unexpected(CookError{"an inverse bind matrix is unreadable"});
                }
                std::memcpy(&out.inverse_bind[j], raw, sizeof(raw));
            }
        }
        asset.skins.push_back(std::move(out));
    }

    std::unordered_map<const cgltf_image*, int> images;
    auto image_index = [&](const cgltf_image* image) -> std::expected<int, CookError> {
        if (image == nullptr) {
            return -1;
        }
        if (const auto found = images.find(image); found != images.end()) {
            return found->second;
        }
        int width = 0;
        int height = 0;
        auto pixels = load_image(image, base_dir, width, height);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        ModelTexture texture;
        texture.width = width;
        texture.height = height;
        texture.rgba = std::move(*pixels);
        const int index = static_cast<int>(asset.textures.size());
        asset.textures.push_back(std::move(texture));
        images.emplace(image, index);
        return index;
    };

    std::unordered_map<const cgltf_material*, int> materials;
    auto material_index = [&](const cgltf_material* material) -> std::expected<int, CookError> {
        if (material == nullptr) {
            return -1;
        }
        if (const auto found = materials.find(material); found != materials.end()) {
            return found->second;
        }
        ModelMaterial out;
        const auto& pbr = material->pbr_metallic_roughness;
        out.base_color = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                pbr.base_color_factor[3]);
        if (pbr.base_color_texture.texcoord != 0) {
            return std::unexpected(CookError{"only the first texture coordinate set is supported"});
        }
        if (pbr.base_color_texture.has_transform) {
            return std::unexpected(CookError{"texture transforms are not supported"});
        }
        const cgltf_texture* texture = pbr.base_color_texture.texture;
        if (texture != nullptr) {
            if (texture->basisu_image != nullptr && texture->image == nullptr) {
                return std::unexpected(CookError{"ktx2 images are not supported"});
            }
            auto image = image_index(texture->image);
            if (!image) {
                return std::unexpected(image.error());
            }
            out.texture = *image;
        }
        if (material->alpha_mode == cgltf_alpha_mode_mask) {
            out.alpha = render::ModelAlpha::Mask;
            out.alpha_cutoff = material->alpha_cutoff;
        } else if (material->alpha_mode == cgltf_alpha_mode_blend) {
            out.alpha = render::ModelAlpha::Blend;
        }
        out.double_sided = material->double_sided;
        out.unlit = material->unlit;
        const int index = static_cast<int>(asset.materials.size());
        asset.materials.push_back(out);
        materials.emplace(material, index);
        return index;
    };

    for (cgltf_size node_i = 0; node_i < data->nodes_count; ++node_i) {
        const cgltf_node& node = data->nodes[node_i];
        if (node.mesh == nullptr) {
            continue;
        }
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& primitive = node.mesh->primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) {
                return std::unexpected(CookError{"only triangle primitives are supported"});
            }
            if (primitive.targets_count > 0 || primitive.has_draco_mesh_compression) {
                return std::unexpected(CookError{"morph targets and mesh compression are not supported"});
            }
            const cgltf_accessor* positions = attribute(&primitive, cgltf_attribute_type_position, 0);
            if (positions == nullptr || positions->is_sparse) {
                const char* reason = positions == nullptr ? "a primitive has no positions" : "sparse accessors are not supported";
                return std::unexpected(CookError{reason});
            }
            ModelPrimitive out;
            out.node = static_cast<int>(node_i);
            out.skin = node.skin == nullptr ? -1 : static_cast<int>(node.skin - data->skins);
            auto material = material_index(primitive.material);
            if (!material) {
                return std::unexpected(material.error());
            }
            out.material = *material;
            const cgltf_size count = positions->count;
            out.mesh.vertices.resize(count);
            const cgltf_accessor* normals = attribute(&primitive, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* uvs = attribute(&primitive, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* colors = attribute(&primitive, cgltf_attribute_type_color, 0);
            const cgltf_accessor* joints = attribute(&primitive, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weights = attribute(&primitive, cgltf_attribute_type_weights, 0);
            const cgltf_accessor* joints1 = attribute(&primitive, cgltf_attribute_type_joints, 1);
            const cgltf_accessor* weights1 = attribute(&primitive, cgltf_attribute_type_weights, 1);
            if (out.skin >= 0 && joints == nullptr) {
                return std::unexpected(CookError{"a skinned primitive has no joints"});
            }
            for (const cgltf_accessor* accessor : {normals, uvs, colors, joints, weights, joints1, weights1}) {
                if (accessor != nullptr && accessor->is_sparse) {
                    return std::unexpected(CookError{"sparse accessors are not supported"});
                }
            }
            if (out.skin >= 0) {
                out.mesh.influences.resize(count);
            }
            for (cgltf_size v = 0; v < count; ++v) {
                float position[3]{};
                if (!read_floats(positions, v, position, 3)) {
                    return std::unexpected(CookError{"a position is unreadable"});
                }
                render::MeshVertex& vertex = out.mesh.vertices[v];
                vertex.position = glm::vec3(position[0], position[1], position[2]);
                vertex.normal = glm::vec3(0, 0, 1);
                if (normals != nullptr) {
                    float normal[3]{};
                    read_floats(normals, v, normal, 3);
                    vertex.normal = glm::vec3(normal[0], normal[1], normal[2]);
                }
                if (uvs != nullptr) {
                    float uv[2]{};
                    read_floats(uvs, v, uv, 2);
                    vertex.uv = glm::vec2(uv[0], uv[1]);
                }
                vertex.color = glm::vec4(1);
                if (colors != nullptr) {
                    float color[4]{1, 1, 1, 1};
                    const int components = static_cast<int>(std::min<cgltf_size>(4, cgltf_num_components(colors->type)));
                    read_floats(colors, v, color, components);
                    vertex.color = glm::vec4(color[0], color[1], color[2], color[3]);
                }
                if (out.skin < 0) {
                    continue;
                }
                float joint_values[8]{};
                float weight_values[8]{};
                read_floats(joints, v, joint_values, 4);
                read_floats(weights, v, weight_values, 4);
                int used = 4;
                if (joints1 != nullptr && weights1 != nullptr) {
                    read_floats(joints1, v, joint_values + 4, 4);
                    read_floats(weights1, v, weight_values + 4, 4);
                    used = 8;
                }
                int order[8]{0, 1, 2, 3, 4, 5, 6, 7};
                std::partial_sort(order, order + 4, order + used, [&](int a, int b) {
                    return weight_values[a] > weight_values[b];
                });
                float sum = 0;
                for (int i = 0; i < 4; ++i) {
                    sum += weight_values[order[i]];
                }
                const bool extra = used > 4;
                render::SkinInfluence& influence = out.mesh.influences[v];
                const int joint_count = static_cast<int>(asset.skins[static_cast<std::size_t>(out.skin)].joints.size());
                for (int i = 0; i < 4; ++i) {
                    const float weight = extra && sum > 0 ? weight_values[order[i]] / sum : weight_values[order[i]];
                    int joint = static_cast<int>(std::lround(joint_values[order[i]]));
                    if (joint < 0 || joint >= joint_count || joint > 255) {
                        return std::unexpected(CookError{"a joint index is outside the skin"});
                    }
                    influence.joints[i] = static_cast<std::uint8_t>(joint);
                    influence.weights[i] = weight;
                }
            }
            if (primitive.indices != nullptr) {
                if (primitive.indices->is_sparse) {
                    return std::unexpected(CookError{"sparse accessors are not supported"});
                }
                out.mesh.indices.resize(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    if (!read_index(primitive.indices, i, out.mesh.indices[i]) || out.mesh.indices[i] >= count) {
                        return std::unexpected(CookError{"an index is unreadable"});
                    }
                }
            } else if (count % 3 != 0) {
                return std::unexpected(CookError{"a non-indexed primitive is not a list of triangles"});
            }
            asset.primitives.push_back(std::move(out));
        }
    }
    if (auto baked = bake(data, asset); !baked) {
        return std::unexpected(baked.error());
    }
    expand_bounds(asset);
    return asset;
}

}

std::expected<void, CookError> apply_markers(ModelAsset& asset, std::span<const ModelMarkerMeta> markers) {
    for (const ModelMarkerMeta& marker : markers) {
        ModelClip* clip = nullptr;
        for (ModelClip& candidate : asset.clips) {
            if (candidate.name == marker.clip) {
                clip = &candidate;
                break;
            }
        }
        if (clip == nullptr) {
            return std::unexpected(CookError{"a marker names an unknown clip"});
        }
        if (!(marker.time >= 0.0f) || marker.time >= clip->duration) {
            return std::unexpected(CookError{"a marker time is outside the clip"});
        }
        clip->markers.push_back(render::ModelMarker{marker.time, marker.name});
    }
    return {};
}

std::expected<std::vector<std::uint8_t>, CookError> cook_gltf(
        std::string_view bytes, const std::filesystem::path& base_dir, std::span<const ModelMarkerMeta> markers) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const cgltf_result parsed = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (parsed != cgltf_result_success || data == nullptr) {
        return fail("the file is not a gltf document");
    }
    const auto path = (base_dir / "model.gltf").string();
    const cgltf_result loaded = cgltf_load_buffers(&options, data, path.c_str());
    if (loaded != cgltf_result_success) {
        cgltf_free(data);
        return fail("a gltf buffer failed to load");
    }
    auto asset = build(data, base_dir);
    cgltf_free(data);
    if (!asset) {
        return std::unexpected(asset.error());
    }
    if (auto marked = apply_markers(*asset, markers); !marked) {
        return std::unexpected(marked.error());
    }
    return render::serialize_model_blob(*asset);
}

}
