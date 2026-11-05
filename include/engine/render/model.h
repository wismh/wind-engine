#pragma once

// docs/tech/features/Assets.md (model files), docs/tech/modules/Render.md (skin)

#include <engine/core/export.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/transform.h>
#include <engine/render/graphic_factory.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// Bones in one skin. A larger palette does not fit the 16 KiB uniform-block minimum of GL 3.3, GLES 3.0, and WebGL2
// once the rest of the lit uniforms are in the same block, so the cooker rejects a skin past this.
inline constexpr int kMaxSkinJoints = 128;

// Baked clip keys are evenly spaced at this rate. The runtime lerps those keys; it does not see glTF splines.
inline constexpr float kModelBakeHz = 30.0f;

enum class ModelAlpha : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

struct ModelNode {
    std::string name;
    int parent = -1;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct ModelSkin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverse_bind;
};

struct ModelMaterial {
    glm::vec4 base_color{1.0f};
    ModelAlpha alpha = ModelAlpha::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool unlit = false;
    int texture = -1;
};

struct ModelTexture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct ModelPrimitive {
    int node = 0;
    int skin = -1;
    int material = -1;
    MeshDesc mesh;
};

// One baked sample of every node's local TRS. `samples.size()` is `key_count * nodes`.
struct ModelNodePose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct ModelClip {
    std::string name;
    float duration = 0.0f;
    int key_count = 0;
    std::vector<ModelNodePose> samples;
};

// CPU model cooked from glTF. Meshes and textures become GPU objects in `spawn_model`, not here.
struct ModelAsset {
    std::vector<ModelNode> nodes;
    std::vector<ModelSkin> skins;
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelMaterial> materials;
    std::vector<ModelTexture> textures;
    std::vector<ModelClip> clips;
};

// Little-endian blob written by the host cooker and read by the runtime. Not glTF.
[[nodiscard]] ENGINE_API std::vector<std::uint8_t> serialize_model_blob(const ModelAsset& model);
[[nodiscard]] ENGINE_API std::optional<ModelAsset> parse_model_blob(std::span<const std::uint8_t> bytes);

// Local TRS of every node at `time`. An unknown clip, or a model with no clips, is the rest pose.
[[nodiscard]] ENGINE_API std::vector<ModelNodePose> sample_model_locals(
        const ModelAsset& model, std::string_view clip, float time, bool loop);

// `alpha` 0 keeps `pose`, 1 replaces it with `from` blended toward `pose`: `pose = mix(from, pose, alpha)`.
ENGINE_API void blend_model_locals(std::vector<ModelNodePose>& pose, const std::vector<ModelNodePose>& from, float alpha);

// Parent-first global matrices in glTF space (the model's root, not the entity that places it in the world).
[[nodiscard]] ENGINE_API std::vector<glm::mat4> model_globals(
        const ModelAsset& model, const std::vector<ModelNodePose>& locals);

// `inverse(global[mesh_node]) * global[joint] * inverse_bind`. Empty when `skin` or `mesh_node` is out of range.
[[nodiscard]] ENGINE_API std::vector<glm::mat4> model_skin_palette(
        const ModelAsset& model, int skin, int mesh_node, const std::vector<glm::mat4>& global);

// TRS of `matrix`. Non-uniform scale above a rotation can leave skew that TRS cannot store; position is exact.
[[nodiscard]] ENGINE_API engine::Transform transform_from_matrix(const glm::mat4& matrix);

// A primitive whose transform this animator writes (`drives_transform`), and whether it skins.
struct ModelPart {
    ecs::Entity entity{};
    int node = 0;
    bool drives_transform = false;
};

// Plays one clip, optionally crossfading out of the previous one. The palette is the skinning matrices for the pose
// just ticked; draw reads it until the next tick.
struct ModelAnimator {
    std::shared_ptr<const ModelAsset> model;
    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = true;
    bool loop = true;
    std::string from_clip;
    float from_time = 0.0f;
    float fade_left = 0.0f;
    float fade_duration = 0.0f;
    std::vector<ModelPart> parts;
    std::vector<ModelNodePose> locals;
    std::vector<glm::mat4> node_global;
    std::vector<std::vector<glm::mat4>> palettes;

    // `fade_seconds` 0 switches immediately. Otherwise the previous pose blends out over that many seconds.
    ENGINE_API void play(std::string_view clip_name, float fade_seconds = 0.0f);
};

// Samples `animator` forward by `dt` seconds and fills `locals`, `node_global`, and `palettes`.
ENGINE_API void tick_model_animator(ModelAnimator& animator, float dt);

// The draw reads `primitive` into `animator`'s palette. `skin` < 0 is a rigid primitive.
struct SkinDraw {
    ecs::Entity animator{};
    int skin = -1;
    int primitive = -1;
};

}
