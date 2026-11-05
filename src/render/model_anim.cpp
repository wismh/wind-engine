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

void ModelAnimator::play(std::string_view clip_name, float fade_seconds) {
    const std::string next(clip_name);
    if (fade_seconds > 0.0f && !clip.empty() && clip != next) {
        from_clip = clip;
        from_time = time;
        fade_left = fade_seconds;
        fade_duration = fade_seconds;
    } else {
        from_clip.clear();
        fade_left = 0.0f;
        fade_duration = 0.0f;
    }
    clip = next;
    time = 0.0f;
    playing = true;
}

void tick_model_animator(ModelAnimator& animator, float dt) {
    if (!animator.model) {
        return;
    }
    const ModelAsset& model = *animator.model;
    if (animator.playing && animator.speed > 0.0f && !animator.clip.empty()) {
        animator.time += dt * animator.speed;
        if (const ModelClip* clip = find_clip(model, animator.clip); clip != nullptr && !animator.loop &&
                                                                      animator.time >= clip->duration) {
            animator.time = clip->duration;
            animator.playing = false;
        }
        if (!animator.from_clip.empty()) {
            animator.from_time += dt * animator.speed;
        }
    }
    if (animator.fade_left > 0.0f) {
        animator.fade_left = std::max(0.0f, animator.fade_left - dt);
        if (animator.fade_left == 0.0f) {
            animator.from_clip.clear();
            animator.fade_duration = 0.0f;
        }
    }

    animator.locals = sample_model_locals(model, animator.clip, animator.time, animator.loop);
    if (!animator.from_clip.empty() && animator.fade_duration > 0.0f) {
        const std::vector<ModelNodePose> from =
                sample_model_locals(model, animator.from_clip, animator.from_time, animator.loop);
        const float alpha = 1.0f - (animator.fade_left / animator.fade_duration);
        blend_model_locals(animator.locals, from, alpha);
    }
    animator.node_global = model_globals(model, animator.locals);
    fill_palettes(model, animator.node_global, animator.palettes);
}

}
