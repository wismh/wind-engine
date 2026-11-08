#include <engine/render/model.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace engine::render {
namespace {

const ModelClip* find_clip(const ModelAsset& model, std::string_view name) {
    for (const ModelClip& clip : model.clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

ModelNodePose rest_pose(const ModelNode& node) {
    return ModelNodePose{node.translation, node.rotation, node.scale};
}

float wrapped_time(const ModelClip& clip, float time, bool loop) {
    if (clip.duration <= 0.0f) {
        return 0.0f;
    }
    if (!loop) {
        return std::clamp(time, 0.0f, clip.duration);
    }
    float wrapped = std::fmod(time, clip.duration);
    if (wrapped < 0.0f) {
        wrapped += clip.duration;
    }
    return wrapped;
}

ModelNodePose sample_key(const ModelClip& clip, int node, float time, bool loop, int node_count) {
    const std::size_t sample_count = static_cast<std::size_t>(clip.key_count) * static_cast<std::size_t>(node_count);
    if (clip.key_count <= 0 || clip.samples.size() != sample_count) {
        return {};
    }
    if (clip.key_count == 1 || clip.duration <= 0.0f) {
        return clip.samples[static_cast<std::size_t>(node)];
    }
    const float wrapped = wrapped_time(clip, time, loop);
    const float scaled = (wrapped / clip.duration) * static_cast<float>(clip.key_count - 1);
    int index = static_cast<int>(std::floor(scaled));
    index = std::clamp(index, 0, clip.key_count - 1);
    const int next = std::min(index + 1, clip.key_count - 1);
    const float alpha = scaled - static_cast<float>(index);
    const std::size_t stride = static_cast<std::size_t>(node_count);
    const ModelNodePose& a = clip.samples[static_cast<std::size_t>(index) * stride + static_cast<std::size_t>(node)];
    const ModelNodePose& b = clip.samples[static_cast<std::size_t>(next) * stride + static_cast<std::size_t>(node)];
    ModelNodePose pose;
    pose.translation = glm::mix(a.translation, b.translation, alpha);
    pose.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, alpha));
    pose.scale = glm::mix(a.scale, b.scale, alpha);
    return pose;
}

void emit_cue(std::vector<ModelAnimCue>& cues, std::string_view clip, std::string_view name) {
    cues.emplace_back(std::string(clip), std::string(name));
}

void emit_window(std::vector<ModelAnimCue>& cues, const ModelClip& clip, float begin, float end, bool include_zero) {
    for (const ModelMarker& marker : clip.markers) {
        const bool at_zero = include_zero && marker.time == 0.0f && begin == 0.0f;
        if (at_zero || (marker.time > begin && marker.time <= end)) {
            emit_cue(cues, clip.name, marker.name);
        }
    }
}

void emit_all(std::vector<ModelAnimCue>& cues, const ModelClip& clip) {
    cues.reserve(cues.size() + clip.markers.size());
    for (const ModelMarker& marker : clip.markers) {
        emit_cue(cues, clip.name, marker.name);
    }
}

// Markers in (previous, next] on the active clip. `from_start` also fires a marker at time 0.
// A loop treats `duration` as the next 0, so a marker at `duration` is not representable.
void collect_cues(std::vector<ModelAnimCue>& cues, const ModelClip& clip, float previous, float next, bool loop,
        bool from_start) {
    if (!(next >= previous) || clip.markers.empty()) {
        return;
    }
    if (clip.duration <= 0.0f) {
        if (from_start) {
            emit_window(cues, clip, 0.0f, 0.0f, true);
        }
        return;
    }
    if (!loop) {
        const float end = std::min(next, clip.duration);
        const float begin = std::min(previous, clip.duration);
        emit_window(cues, clip, begin, end, from_start && begin == 0.0f);
        return;
    }

    const float duration = clip.duration;
    const float start_mod = wrapped_time(clip, previous, true);
    const int start_loop = static_cast<int>(std::floor(static_cast<double>(previous) / static_cast<double>(duration)));
    const int end_loop = static_cast<int>(std::floor(static_cast<double>(next) / static_cast<double>(duration)));
    if (end_loop == start_loop) {
        emit_window(cues, clip, start_mod, wrapped_time(clip, next, true), from_start && start_mod == 0.0f);
        return;
    }

    for (const ModelMarker& marker : clip.markers) {
        const bool at_zero = from_start && marker.time == 0.0f && start_mod == 0.0f;
        if (at_zero || (marker.time > start_mod && marker.time < duration)) {
            emit_cue(cues, clip.name, marker.name);
        }
    }
    const int full_loops = end_loop - start_loop - 1;
    for (int loop_index = 0; loop_index < full_loops; ++loop_index) {
        emit_all(cues, clip);
    }
    const float end_mod = wrapped_time(clip, next, true);
    for (const ModelMarker& marker : clip.markers) {
        if (marker.time == 0.0f || (marker.time > 0.0f && marker.time <= end_mod)) {
            emit_cue(cues, clip.name, marker.name);
        }
    }
}

void fill_palettes(const ModelAsset& model, const std::vector<glm::mat4>& global, std::vector<std::vector<glm::mat4>>& palettes) {
    palettes.assign(model.primitives.size(), {});
    for (std::size_t i = 0; i < model.primitives.size(); ++i) {
        const ModelPrimitive& primitive = model.primitives[i];
        if (primitive.skin < 0) {
            continue;
        }
        palettes[i] = model_skin_palette(model, primitive.skin, primitive.node, global);
    }
}

}

std::vector<ModelNodePose> sample_model_locals(const ModelAsset& model, std::string_view clip, float time, bool loop) {
    std::vector<ModelNodePose> locals(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        locals[i] = rest_pose(model.nodes[i]);
    }
    const ModelClip* found = find_clip(model, clip);
    if (found == nullptr) {
        return locals;
    }
    const int node_count = static_cast<int>(model.nodes.size());
    for (int node = 0; node < node_count; ++node) {
        locals[static_cast<std::size_t>(node)] = sample_key(*found, node, time, loop, node_count);
    }
    return locals;
}

void blend_model_locals(std::vector<ModelNodePose>& pose, const std::vector<ModelNodePose>& from, float alpha) {
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    const std::size_t count = std::min(pose.size(), from.size());
    for (std::size_t i = 0; i < count; ++i) {
        pose[i].translation = glm::mix(from[i].translation, pose[i].translation, t);
        pose[i].rotation = glm::normalize(glm::slerp(from[i].rotation, pose[i].rotation, t));
        pose[i].scale = glm::mix(from[i].scale, pose[i].scale, t);
    }
}

namespace {

void blend_one(ModelNodePose& pose, const ModelNodePose& over, float weight) {
    const float t = std::clamp(weight, 0.0f, 1.0f);
    if (t <= 0.0f) {
        return;
    }
    pose.translation = glm::mix(pose.translation, over.translation, t);
    pose.rotation = glm::normalize(glm::slerp(pose.rotation, over.rotation, t));
    pose.scale = glm::mix(pose.scale, over.scale, t);
}

}

void blend_model_locals(std::vector<ModelNodePose>& pose, const std::vector<ModelNodePose>& over,
        std::span<const float> weight) {
    const std::size_t count = std::min(pose.size(), over.size());
    const std::size_t weighted = std::min(count, weight.size());
    for (std::size_t i = 0; i < weighted; ++i) {
        blend_one(pose[i], over[i], weight[i]);
    }
}

namespace {

int find_node(const ModelAsset& model, std::string_view name) {
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool node_in_mask(const ModelAsset& model, int node, int mask_root) {
    int current = node;
    for (std::size_t step = 0; step < model.nodes.size() && current >= 0; ++step) {
        if (current == mask_root) {
            return true;
        }
        const int parent = model.nodes[static_cast<std::size_t>(current)].parent;
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return false;
}

std::vector<float> layer_weights(const ModelAsset& model, const ModelLayer& layer) {
    std::vector<float> weights(model.nodes.size(), 0.0f);
    const float weight = std::clamp(layer.weight, 0.0f, 1.0f);
    if (weight <= 0.0f || layer.clip.empty()) {
        return weights;
    }
    if (layer.mask.empty()) {
        std::fill(weights.begin(), weights.end(), weight);
        return weights;
    }
    const int mask_root = find_node(model, layer.mask);
    if (mask_root < 0) {
        return weights;
    }
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (node_in_mask(model, static_cast<int>(i), mask_root)) {
            weights[i] = weight;
        }
    }
    return weights;
}

std::vector<ModelNodePose> rest_locals(const ModelAsset& model) {
    std::vector<ModelNodePose> locals(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        locals[i] = rest_pose(model.nodes[i]);
    }
    return locals;
}

std::vector<ModelNodePose> layer_pose(const ModelAsset& model, const ModelLayer& layer) {
    std::vector<ModelNodePose> pose = sample_model_locals(model, layer.clip, layer.time, layer.loop);
    if (!layer.from_clip.empty() && layer.fade_duration > 0.0f) {
        const std::vector<ModelNodePose> from =
                sample_model_locals(model, layer.from_clip, layer.from_time, layer.loop);
        const float alpha = 1.0f - (layer.fade_left / layer.fade_duration);
        blend_model_locals(pose, from, alpha);
    }
    return pose;
}

void add_delta(ModelNodePose& pose, const ModelNodePose& clip, const ModelNodePose& rest, float weight) {
    const float t = std::clamp(weight, 0.0f, 1.0f);
    if (t <= 0.0f) {
        return;
    }
    pose.translation += (clip.translation - rest.translation) * t;
    pose.scale += (clip.scale - rest.scale) * t;
    const glm::quat delta = clip.rotation * glm::inverse(rest.rotation);
    pose.rotation = glm::normalize(glm::slerp(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, delta, t) * pose.rotation);
}

void apply_layer(std::vector<ModelNodePose>& pose, const ModelAsset& model, const ModelLayer& layer,
        const std::vector<ModelNodePose>& rest) {
    if (layer.clip.empty() || layer.weight <= 0.0f) {
        return;
    }
    const std::vector<float> weights = layer_weights(model, layer);
    const std::vector<ModelNodePose> over = layer_pose(model, layer);
    const std::size_t count = std::min(pose.size(), std::min(over.size(), weights.size()));
    if (layer.blend == ModelBlend::Additive) {
        for (std::size_t i = 0; i < count; ++i) {
            const ModelNodePose& bind = i < rest.size() ? rest[i] : ModelNodePose{};
            add_delta(pose[i], over[i], bind, weights[i]);
        }
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        blend_one(pose[i], over[i], weights[i]);
    }
}

void advance_layer(ModelLayer& layer, const ModelAsset& model, float dt, std::vector<ModelAnimCue>& cues) {
    const float previous = layer.time;
    const bool from_start = layer.cue_from_start;
    if (layer.playing && layer.speed > 0.0f && !layer.clip.empty()) {
        layer.time += dt * layer.speed;
        if (const ModelClip* clip = find_clip(model, layer.clip); clip != nullptr && !layer.loop &&
                                                                    layer.time >= clip->duration) {
            layer.time = clip->duration;
            layer.playing = false;
        }
        if (!layer.from_clip.empty()) {
            layer.from_time += dt * layer.speed;
        }
        const bool mask_miss = !layer.mask.empty() && find_node(model, layer.mask) < 0;
        if (!mask_miss && std::clamp(layer.weight, 0.0f, 1.0f) > 0.0f) {
            if (const ModelClip* clip = find_clip(model, layer.clip); clip != nullptr) {
                collect_cues(cues, *clip, previous, layer.time, layer.loop, from_start);
            }
        }
        layer.cue_from_start = false;
    }
    if (layer.fade_left > 0.0f) {
        layer.fade_left = std::max(0.0f, layer.fade_left - dt);
        if (layer.fade_left == 0.0f) {
            layer.from_clip.clear();
            layer.fade_duration = 0.0f;
        }
    }
}

constexpr float kSpringStep = 1.0f / 60.0f;
constexpr float kSpringStop = 1.0e-3f;

glm::vec3 rotation_vector(glm::quat rotation) {
    rotation = glm::normalize(rotation);
    const float angle = glm::angle(rotation);
    if (angle <= 1.0e-6f) {
        return {};
    }
    return glm::axis(rotation) * angle;
}

void integrate_spring(ModelJointSpring& spring, float step) {
    const glm::vec3 linear = -spring.stiffness * spring.translation - spring.damping * spring.linear_velocity;
    spring.linear_velocity += linear * step;
    spring.translation += spring.linear_velocity * step;

    const glm::vec3 angular =
            -spring.stiffness * rotation_vector(spring.rotation) - spring.damping * spring.angular_velocity;
    spring.angular_velocity += angular * step;
    const float speed = glm::length(spring.angular_velocity);
    if (speed > 1.0e-8f) {
        spring.rotation = glm::normalize(
                glm::angleAxis(speed * step, spring.angular_velocity / speed) * glm::normalize(spring.rotation));
    }
}

bool spring_settled(const ModelJointSpring& spring) {
    return glm::length(spring.translation) < kSpringStop && glm::angle(glm::normalize(spring.rotation)) < kSpringStop &&
           glm::length(spring.linear_velocity) < kSpringStop && glm::length(spring.angular_velocity) < kSpringStop;
}

void integrate_springs(ModelAnimator& animator, float dt) {
    if (dt <= 0.0f) {
        return;
    }
    for (std::size_t i = 0; i < animator.springs.size();) {
        ModelJointSpring& spring = animator.springs[i];
        float left = dt;
        while (left > 0.0f) {
            const float step = std::min(kSpringStep, left);
            integrate_spring(spring, step);
            left -= step;
        }
        const bool missing = spring.node < 0 || static_cast<std::size_t>(spring.node) >= animator.locals.size();
        if (missing || spring_settled(spring)) {
            animator.springs.erase(animator.springs.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ModelNodePose& pose = animator.locals[static_cast<std::size_t>(spring.node)];
        pose.translation += spring.translation;
        pose.rotation = glm::normalize(spring.rotation * pose.rotation);
        ++i;
    }
}

}

std::vector<glm::mat4> model_globals(const ModelAsset& model, const std::vector<ModelNodePose>& locals) {
    std::vector<glm::mat4> global(model.nodes.size(), glm::mat4{1.0f});
    const std::size_t count = std::min(model.nodes.size(), locals.size());
    // Nodes are stored parent-before-child by the cooker, so one forward pass is enough. A parent that is not is
    // resolved by repeating the pass until it settles, bounded by the node count.
    for (std::size_t pass = 0; pass < count; ++pass) {
        bool pending = false;
        for (std::size_t i = 0; i < count; ++i) {
            const ModelNodePose& pose = locals[i];
            const glm::mat4 local = glm::translate(glm::mat4{1.0f}, pose.translation) * glm::mat4_cast(pose.rotation) *
                                    glm::scale(glm::mat4{1.0f}, pose.scale);
            const int parent = model.nodes[i].parent;
            if (parent < 0) {
                global[i] = local;
                continue;
            }
            if (static_cast<std::size_t>(parent) >= count) {
                global[i] = local;
                continue;
            }
            if (static_cast<std::size_t>(parent) > i && pass + 1 < count) {
                pending = true;
                continue;
            }
            global[i] = global[static_cast<std::size_t>(parent)] * local;
        }
        if (!pending) {
            break;
        }
    }
    return global;
}

std::vector<glm::mat4> model_skin_palette(
        const ModelAsset& model, int skin, int mesh_node, const std::vector<glm::mat4>& global) {
    if (skin < 0 || static_cast<std::size_t>(skin) >= model.skins.size() || mesh_node < 0 ||
        static_cast<std::size_t>(mesh_node) >= global.size()) {
        return {};
    }
    const ModelSkin& joints = model.skins[static_cast<std::size_t>(skin)];
    const glm::mat4 inverse_mesh = glm::inverse(global[static_cast<std::size_t>(mesh_node)]);
    std::vector<glm::mat4> palette(joints.joints.size(), glm::mat4{1.0f});
    for (std::size_t i = 0; i < joints.joints.size(); ++i) {
        const int joint = joints.joints[i];
        const glm::mat4 joint_global = joint >= 0 && static_cast<std::size_t>(joint) < global.size()
                                               ? global[static_cast<std::size_t>(joint)]
                                               : glm::mat4{1.0f};
        const glm::mat4 inverse_bind = i < joints.inverse_bind.size() ? joints.inverse_bind[i] : glm::mat4{1.0f};
        palette[i] = inverse_mesh * joint_global * inverse_bind;
    }
    return palette;
}

engine::Transform transform_from_matrix(const glm::mat4& matrix) {
    engine::Transform transform;
    transform.position = glm::vec3(matrix[3]);
    glm::vec3 x = glm::vec3(matrix[0]);
    glm::vec3 y = glm::vec3(matrix[1]);
    glm::vec3 z = glm::vec3(matrix[2]);
    transform.scale = glm::vec3(glm::length(x), glm::length(y), glm::length(z));
    if (transform.scale.x != 0.0f) {
        x /= transform.scale.x;
    }
    if (transform.scale.y != 0.0f) {
        y /= transform.scale.y;
    }
    if (transform.scale.z != 0.0f) {
        z /= transform.scale.z;
    }
    if (glm::determinant(glm::mat3(x, y, z)) < 0.0f) {
        transform.scale.x = -transform.scale.x;
        x = -x;
    }
    transform.rotation = glm::quat_cast(glm::mat3(x, y, z));
    return transform;
}

void ModelAnimator::play_layer(int index, std::string_view clip_name, float fade_seconds) {
    if (index < 0) {
        return;
    }
    if (layers.size() <= static_cast<std::size_t>(index)) {
        layers.resize(static_cast<std::size_t>(index) + 1);
    }
    ModelLayer& layer = layers[static_cast<std::size_t>(index)];
    const std::string next(clip_name);
    if (fade_seconds > 0.0f && !layer.clip.empty() && layer.clip != next) {
        layer.from_clip = layer.clip;
        layer.from_time = layer.time;
        layer.fade_left = fade_seconds;
        layer.fade_duration = fade_seconds;
    } else {
        layer.from_clip.clear();
        layer.fade_left = 0.0f;
        layer.fade_duration = 0.0f;
    }
    layer.clip = next;
    layer.time = 0.0f;
    layer.playing = true;
    layer.cue_from_start = true;
    cues.clear();
}

void ModelAnimator::play(std::string_view clip_name, float fade_seconds) {
    play_layer(0, clip_name, fade_seconds);
}

void tick_model_animator(ModelAnimator& animator, float dt) {
    animator.cues.clear();
    if (!animator.model) {
        return;
    }
    const ModelAsset& model = *animator.model;
    for (ModelLayer& layer : animator.layers) {
        advance_layer(layer, model, dt, animator.cues);
    }
    const std::vector<ModelNodePose> rest = rest_locals(model);
    animator.locals = rest;
    for (const ModelLayer& layer : animator.layers) {
        apply_layer(animator.locals, model, layer, rest);
    }
    integrate_springs(animator, dt);
    animator.node_global = model_globals(model, animator.locals);
    fill_palettes(model, animator.node_global, animator.palettes);
}

void impulse_joint(ModelAnimator& animator, std::string_view node_name, const glm::vec3& world_direction,
        float strength, const glm::mat4& root_world) {
    if (!animator.model || strength == 0.0f || glm::dot(world_direction, world_direction) <= 1.0e-12f) {
        return;
    }
    const ModelAsset& model = *animator.model;
    const int node = find_node(model, node_name);
    if (node < 0) {
        return;
    }

    std::vector<glm::mat4> owned;
    const std::vector<glm::mat4>* globals = &animator.node_global;
    if (animator.node_global.size() != model.nodes.size()) {
        owned = model_globals(model, rest_locals(model));
        globals = &owned;
    }
    const glm::vec3 model_dir = glm::inverse(transform_from_matrix(root_world).rotation) * world_direction;
    if (glm::dot(model_dir, model_dir) <= 1.0e-12f) {
        return;
    }
    const int parent = model.nodes[static_cast<std::size_t>(node)].parent;
    glm::quat parent_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (parent >= 0 && static_cast<std::size_t>(parent) < globals->size()) {
        parent_rotation = transform_from_matrix((*globals)[static_cast<std::size_t>(parent)]).rotation;
    }
    glm::vec3 local_dir = glm::inverse(parent_rotation) * model_dir;
    if (glm::dot(local_dir, local_dir) <= 1.0e-12f) {
        return;
    }
    local_dir = glm::normalize(local_dir);

    glm::vec3 bone_axis = model.nodes[static_cast<std::size_t>(node)].translation;
    if (animator.locals.size() == model.nodes.size()) {
        bone_axis = animator.locals[static_cast<std::size_t>(node)].translation;
    }
    if (glm::dot(bone_axis, bone_axis) <= 1.0e-8f) {
        bone_axis = glm::vec3{0.0f, 1.0f, 0.0f};
    } else {
        bone_axis = glm::normalize(bone_axis);
    }
    glm::vec3 axis = glm::cross(bone_axis, local_dir);
    if (glm::dot(axis, axis) <= 1.0e-8f) {
        axis = glm::cross(local_dir, glm::vec3{0.0f, 1.0f, 0.0f});
        if (glm::dot(axis, axis) <= 1.0e-8f) {
            axis = glm::cross(local_dir, glm::vec3{1.0f, 0.0f, 0.0f});
        }
    }
    axis = glm::normalize(axis);

    ModelJointSpring* spring = nullptr;
    for (ModelJointSpring& existing : animator.springs) {
        if (existing.node == node) {
            spring = &existing;
            break;
        }
    }
    if (spring == nullptr) {
        animator.springs.push_back(ModelJointSpring{});
        spring = &animator.springs.back();
        spring->node = node;
    }
    spring->linear_velocity += local_dir * strength;
    spring->angular_velocity += axis * strength;
}

}
