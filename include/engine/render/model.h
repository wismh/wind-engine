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

// A named instant on a clip, in seconds, in `[0, duration)`. Authored as `[[markers]]` on the model's `.meta`.
struct ModelMarker {
    float time = 0.0f;
    std::string name;
};

struct ModelClip {
    std::string name;
    float duration = 0.0f;
    int key_count = 0;
    std::vector<ModelNodePose> samples;
    std::vector<ModelMarker> markers;
};

// One marker the last tick crossed on the active clip.
struct ModelAnimCue {
    std::string clip;
    std::string name;
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

// `alpha` 0 keeps `from`, 1 keeps `pose`: `pose = mix(from, pose, alpha)`.
ENGINE_API void blend_model_locals(
        std::vector<ModelNodePose>& pose, const std::vector<ModelNodePose>& from, float alpha);

// Per node, `weight` 0 keeps `pose` and 1 takes `over`. A short `weight` leaves the remaining nodes alone.
ENGINE_API void blend_model_locals(
        std::vector<ModelNodePose>& pose, const std::vector<ModelNodePose>& over, std::span<const float> weight);

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

// How a layer combines with the pose under it. `Override` replaces masked bones. `Additive` adds the clip's delta from
// the rest pose on those bones.
enum class ModelBlend : std::uint8_t {
    Override,
    Additive,
};

// One clip on top of the pose. `layers[0]` is the base `play` writes. An empty `mask` is the whole body; otherwise the
// named node and its descendants. A name that matches no node leaves the pose unchanged and sends no markers. `weight`
// is clamped to 0..1. Weight 0 advances the clock and sends no markers.
struct ModelLayer {
    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = true;
    bool loop = true;
    float weight = 1.0f;
    ModelBlend blend = ModelBlend::Override;
    std::string mask;
    std::string from_clip;
    float from_time = 0.0f;
    float fade_left = 0.0f;
    float fade_duration = 0.0f;
    // `play_layer` sets this. The next tick that advances the clip also fires markers at time 0.
    bool cue_from_start = false;
};

// Offset of one node from the clip pose, in that node's parent space. `tick_model_animator` springs it back to zero.
struct ModelJointSpring {
    int node = -1;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 translation{0.0f};
    glm::vec3 angular_velocity{0.0f};
    glm::vec3 linear_velocity{0.0f};
    float stiffness = 80.0f;
    float damping = 18.0f;
};

// Plays layered clips, then joint springs. The palette is the skinning matrices for the pose just ticked; draw reads
// it until the next tick. Layer 0 is the base clip.
struct ModelAnimator {
    std::shared_ptr<const ModelAsset> model;
    std::vector<ModelLayer> layers;
    std::vector<ModelPart> parts;
    std::vector<ModelNodePose> locals;
    std::vector<glm::mat4> node_global;
    std::vector<std::vector<glm::mat4>> palettes;
    // Markers the last `tick_model_animator` crossed on every layer whose weight is above 0.
    std::vector<ModelAnimCue> cues;
    std::vector<ModelJointSpring> springs;

    // Layer 0. `fade_seconds` 0 switches immediately. Otherwise the previous pose blends out over that many seconds.
    // Markers follow the new clip's clock, including one at time 0. The clip being faded out stays silent.
    ENGINE_API void play(std::string_view clip_name, float fade_seconds = 0.0f);

    // Ensures `layers` reaches `index`, then starts that layer the way `play` starts layer 0. A negative index does
    // nothing. Weight, mask, blend, speed, and loop stay as they are; a new slot starts at weight 1, full body.
    ENGINE_API void play_layer(int index, std::string_view clip_name, float fade_seconds = 0.0f);
};

// Adds a hit on `node_name`. Linear velocity is `strength` meters/second along `world_direction` in the node's parent
// space, plus an angular velocity that swings the bone off that direction. `root_world` maps the direction out of
// world space; identity is correct when the model root is unrotated or the direction is already in model space.
// An unknown name, a zero direction, or a zero strength does nothing. A second call in the same tick adds velocity.
ENGINE_API void impulse_joint(ModelAnimator& animator, std::string_view node_name, const glm::vec3& world_direction,
        float strength, const glm::mat4& root_world = glm::mat4{1.0f});

// Samples `animator` forward by `dt` seconds and fills `locals`, `node_global`, and `palettes`.
ENGINE_API void tick_model_animator(ModelAnimator& animator, float dt);

// The draw reads `primitive` into `animator`'s palette. `skin` < 0 is a rigid primitive.
struct SkinDraw {
    ecs::Entity animator{};
    int skin = -1;
    int primitive = -1;
};

}
