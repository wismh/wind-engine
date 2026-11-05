#pragma once

#include "gl_includes.h"

#include "context_vao.h"

#include <engine/render/post_process.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <vector>

namespace engine::render {

// Scene color targets for one GL context. Textures are shared across the engine's contexts; the framebuffer names
// are not, so each context keeps its own. An inactive stack never calls `begin_scene`, and nothing is allocated.
class OpenGLPost {
public:
    OpenGLPost() = default;
    ~OpenGLPost();

    OpenGLPost(const OpenGLPost&) = delete;
    OpenGLPost& operator=(const OpenGLPost&) = delete;

    void set_surface(int width, int height, float clear_alpha) noexcept;
    void begin_scene(const PostChain& chain);
    void finish(const PostChain& chain, float time);
    void bind_window();

    [[nodiscard]] bool scene_bound() const noexcept { return scene_bound_; }

private:
    struct Targets {
        ContextObjects::Key key = 0;
        int width = 0;
        int height = 0;
        SceneColorFormat format = SceneColorFormat::Rgba8;
        bool depth_texture = false;
        GLuint color[2]{};
        GLuint fbo[2]{};
        GLuint depth = 0;
        GLuint bloom_color[3]{};
        GLuint bloom_fbo[3]{};
        int bloom_width[3]{};
        int bloom_height[3]{};
    };

    [[nodiscard]] bool float_renderable(SceneColorFormat format);
    [[nodiscard]] Targets& ensure(const PostChain& chain, bool hdr);
    void destroy_targets(Targets& targets, bool delete_fbos);
    void draw(const std::shared_ptr<IShader>& shader, const glm::vec4& params, const glm::vec2& resolution, float time,
            GLuint source, GLuint dest_fbo, int dest_width, int dest_height, GLuint depth, bool additive);
    void draw_bloom(const PostPass& pass, GLuint source, int source_width, int source_height, GLuint dest_fbo,
            int dest_width, int dest_height, Targets& targets, float time);

    std::vector<Targets> targets_;
    ContextVao triangle_;
    int width_ = 0;
    int height_ = 0;
    float clear_alpha_ = 1.0f;
    bool scene_bound_ = false;
    bool hdr_effective_ = false;
    bool r11_checked_ = false;
    bool r11_ok_ = false;
    bool rgba16_checked_ = false;
    bool rgba16_ok_ = false;
    bool fallback_logged_ = false;
};

} // namespace engine::render
