#include "opengl_post.h"

#include "context_vao.h"
#include "opengl_shader.h"

#include <engine/log.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <string>

namespace engine::render {

namespace {

void color_format(SceneColorFormat format, GLenum& internal, GLenum& layout, GLenum& type) {
    if (format == SceneColorFormat::R11G11B10) {
        internal = GL_R11F_G11F_B10F;
        layout = GL_RGB;
        type = GL_FLOAT;
        return;
    }
    if (format == SceneColorFormat::Rgba16F) {
        internal = GL_RGBA16F;
        layout = GL_RGBA;
        type = GL_FLOAT;
        return;
    }
#if defined(ENGINE_WITH_GLES)
    internal = GL_RGBA8;
#else
    internal = GL_RGBA;
#endif
    layout = GL_RGBA;
    type = GL_UNSIGNED_BYTE;
}

GLuint make_color(int width, int height, SceneColorFormat format) {
    GLenum internal = GL_RGBA;
    GLenum layout = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    color_format(format, internal, layout, type);
    GLuint name = 0;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), width, height, 0, layout, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return name;
}

GLuint make_fbo(GLuint color, GLuint depth, bool depth_is_texture) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth != 0) {
        if (depth_is_texture) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        } else {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        }
    }
    return fbo;
}

GLuint make_depth(int width, int height, bool as_texture) {
    GLuint name = 0;
    if (as_texture) {
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return name;
    }
    glGenRenderbuffers(1, &name);
    glBindRenderbuffer(GL_RENDERBUFFER, name);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return name;
}

} // namespace

OpenGLPost::~OpenGLPost() {
    if (current_gl_context() == 0) {
        return;
    }
    for (Targets& targets : targets_) {
        destroy_targets(targets, targets.key == current_gl_context());
    }
    targets_.clear();
}

void OpenGLPost::set_surface(int width, int height, float clear_alpha) noexcept {
    width_ = width;
    height_ = height;
    clear_alpha_ = clear_alpha;
}

bool OpenGLPost::float_renderable(SceneColorFormat format) {
    bool& checked = format == SceneColorFormat::Rgba16F ? rgba16_checked_ : r11_checked_;
    bool& ok = format == SceneColorFormat::Rgba16F ? rgba16_ok_ : r11_ok_;
    if (checked) {
        return ok;
    }
    checked = true;
    const GLuint color = make_color(1, 1, format);
    const GLuint fbo = make_fbo(color, 0, false);
    ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color);
    return ok;
}

void OpenGLPost::destroy_targets(Targets& targets, bool delete_fbos) {
    glDeleteTextures(2, targets.color);
    glDeleteTextures(3, targets.bloom_color);
    if (targets.depth != 0) {
        if (targets.depth_texture) {
            glDeleteTextures(1, &targets.depth);
        } else if (delete_fbos) {
            glDeleteRenderbuffers(1, &targets.depth);
        }
    }
    if (delete_fbos) {
        glDeleteFramebuffers(2, targets.fbo);
        glDeleteFramebuffers(3, targets.bloom_fbo);
    }
    targets = Targets{};
}

OpenGLPost::Targets& OpenGLPost::ensure(const PostChain& chain, bool hdr) {
    const ContextObjects::Key key = current_gl_context();
    const bool want_depth = chain.samples_depth();
    const SceneColorFormat format =
            scene_color_format(hdr, clear_alpha_ < 1.0f, hdr ? float_renderable(clear_alpha_ < 1.0f
                                                                                        ? SceneColorFormat::Rgba16F
                                                                                        : SceneColorFormat::R11G11B10)
                                                              : true);
    Targets* found = nullptr;
    for (Targets& targets : targets_) {
        if (targets.key == key) {
            found = &targets;
            break;
        }
    }
    if (found == nullptr) {
        targets_.push_back(Targets{.key = key});
        found = &targets_.back();
    }
    const bool same = found->width == width_ && found->height == height_ && found->format == format &&
                      found->depth_texture == want_depth && found->color[0] != 0;
    if (same) {
        return *found;
    }
    destroy_targets(*found, true);
    found->key = key;
    found->width = width_;
    found->height = height_;
    found->format = format;
    found->depth_texture = want_depth;
    found->color[0] = make_color(width_, height_, format);
    found->color[1] = make_color(width_, height_, format);
    found->depth = make_depth(width_, height_, want_depth);
    found->fbo[0] = make_fbo(found->color[0], found->depth, want_depth);
    found->fbo[1] = make_fbo(found->color[1], 0, false);
    int level_width = std::max(1, width_ / 2);
    int level_height = std::max(1, height_ / 2);
    for (int level = 0; level < 3; ++level) {
        found->bloom_width[level] = level_width;
        found->bloom_height[level] = level_height;
        found->bloom_color[level] = make_color(level_width, level_height, format);
        found->bloom_fbo[level] = make_fbo(found->bloom_color[level], 0, false);
        level_width = std::max(1, level_width / 2);
        level_height = std::max(1, level_height / 2);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return *found;
}

void OpenGLPost::begin_scene(const PostChain& chain) {
    if (width_ <= 0 || height_ <= 0 || current_gl_context() == 0) {
        return;
    }
    const bool want_float = chain.hdr;
    const SceneColorFormat needed = want_float
            ? (clear_alpha_ < 1.0f ? SceneColorFormat::Rgba16F : SceneColorFormat::R11G11B10)
            : SceneColorFormat::Rgba8;
    const bool supported = !want_float || float_renderable(needed);
    hdr_effective_ = want_float && supported;
    if (want_float && !supported && !fallback_logged_) {
        fallback_logged_ = true;
        log::warn("Post stack HDR has no float color attachment; the stack runs in RGBA8");
    }
    Targets& targets = ensure(chain, hdr_effective_);
    glBindFramebuffer(GL_FRAMEBUFFER, targets.fbo[0]);
    glViewport(0, 0, targets.width, targets.height);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, clear_alpha_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    scene_bound_ = true;
}

void OpenGLPost::bind_window() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (width_ > 0 && height_ > 0) {
        glViewport(0, 0, width_, height_);
    }
    glDisable(GL_SCISSOR_TEST);
    scene_bound_ = false;
}

void OpenGLPost::draw(const std::shared_ptr<IShader>& shader, const glm::vec4& params, const glm::vec2& resolution,
        float time, GLuint source, GLuint dest_fbo, int dest_width, int dest_height, GLuint depth, bool additive) {
    const auto program = std::dynamic_pointer_cast<OpenGLShader>(shader);
    if (!program || !program->valid() || dest_width <= 0 || dest_height <= 0) {
        return;
    }
    const GLuint vao = triangle_.current([] {});
    if (vao == 0) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, dest_fbo);
    glViewport(0, 0, dest_width, dest_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    if (additive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }
    program->use();
    program->set_vec4("uParams", params);
    program->set_vec2("uResolution", resolution);
    program->set_float("uTime", time);
    program->set_int("uScene", 0);
    program->set_int("uDepth", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depth);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void OpenGLPost::draw_bloom(const PostPass& pass, GLuint source, int source_width, int source_height, GLuint dest_fbo,
        int dest_width, int dest_height, Targets& targets, float time) {
    glm::vec4 params = pass.params;
    params.z = 3.0f;
    draw(pass.shader, params, glm::vec2{source_width, source_height}, time, source, dest_fbo, dest_width, dest_height,
            0, false);
    GLuint level_source = source;
    int level_width = source_width;
    int level_height = source_height;
    for (int level = 0; level < 3; ++level) {
        params.z = level == 0 ? 0.0f : 1.0f;
        draw(pass.shader, params, glm::vec2{level_width, level_height}, time, level_source, targets.bloom_fbo[level],
                targets.bloom_width[level], targets.bloom_height[level], 0, false);
        level_source = targets.bloom_color[level];
        level_width = targets.bloom_width[level];
        level_height = targets.bloom_height[level];
    }
    params.z = 2.0f;
    for (int level = 0; level < 3; ++level) {
        draw(pass.shader, params, glm::vec2{targets.bloom_width[level], targets.bloom_height[level]}, time,
                targets.bloom_color[level], dest_fbo, dest_width, dest_height, 0, true);
    }
}

void OpenGLPost::finish(const PostChain& chain, float time) {
    if (!scene_bound_ || width_ <= 0 || height_ <= 0) {
        bind_window();
        return;
    }
    Targets* targets = nullptr;
    const ContextObjects::Key key = current_gl_context();
    for (Targets& entry : targets_) {
        if (entry.key == key) {
            targets = &entry;
            break;
        }
    }
    if (targets == nullptr) {
        bind_window();
        return;
    }
    const std::vector<PostStep> steps = post_steps(hdr_effective_, static_cast<int>(chain.effects.size()));
    for (const PostStep& step : steps) {
        const GLuint source = step.source == PostImage::SceneA ? targets->color[0] : targets->color[1];
        const bool to_window = step.dest == PostImage::Window;
        const GLuint dest_fbo = to_window ? 0u : targets->fbo[step.dest == PostImage::SceneA ? 0 : 1];
        const int dest_width = to_window ? width_ : targets->width;
        const int dest_height = to_window ? height_ : targets->height;
        if (step.tonemap) {
            draw(chain.tonemap, glm::vec4{1.0f}, glm::vec2{width_, height_}, time, source, dest_fbo, dest_width,
                    dest_height, 0, false);
            continue;
        }
        if (step.effect < 0 || static_cast<std::size_t>(step.effect) >= chain.effects.size()) {
            continue;
        }
        const PostPass& pass = chain.effects[static_cast<std::size_t>(step.effect)];
        if (pass.bloom) {
            draw_bloom(pass, source, targets->width, targets->height, dest_fbo, dest_width, dest_height, *targets,
                    time);
        } else {
            draw(pass.shader, pass.params, glm::vec2{targets->width, targets->height}, time, source, dest_fbo,
                    dest_width, dest_height, pass.sample_depth ? targets->depth : 0u, false);
        }
    }
    bind_window();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

} // namespace engine::render
