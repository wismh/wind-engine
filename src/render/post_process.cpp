#include <engine/render/post_process.h>

#include <engine/builtin_ids.h>
#include <engine/render/command_buffer.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>

#include <utility>

namespace engine::render {

namespace {

[[nodiscard]] std::shared_ptr<IShader> load_shader(AssetsDb* assets, IFatalError* fatal, AssetId id, std::string_view what) {
    if (fatal == nullptr) {
        return {};
    }
    if (assets == nullptr) {
        fatal->report(std::string(what) + " needs AssetsDb");
    }
    auto loaded = assets->try_get<IShader>(id);
    if (!loaded) {
        fatal->report(std::string(what) + " shader " + std::string(id.hex()) + " did not load (" +
                      std::string(to_string(loaded.error())) + ")");
    }
    return std::move(*loaded);
}

} // namespace

std::expected<PreparedPost, std::string> prepare_post_stack(const PostStack* stack) {
    PreparedPost prepared;
    if (stack == nullptr || !stack->active()) {
        return prepared;
    }
    prepared.hdr = stack->hdr;
    prepared.effects.reserve(stack->effects.size());
    for (const PostEffect& effect : stack->effects) {
        if (effect.shader == builtin::shader_post_bloom && !stack->hdr) {
            return std::unexpected("Bloom post effect requires PostStack::hdr");
        }
        PostEffect copy = effect;
        if (effect.shader == builtin::shader_post_depth) {
            copy.sample_depth = true;
        }
        prepared.effects.push_back(copy);
    }
    return prepared;
}

PostChain resolve_post_chain(const PreparedPost& prepared, AssetsDb* assets, IFatalError* fatal) {
    PostChain chain;
    if (!prepared.hdr && prepared.effects.empty()) {
        return chain;
    }
    chain.hdr = prepared.hdr;
    if (prepared.hdr) {
        chain.tonemap = load_shader(assets, fatal, builtin::shader_post_tonemap, "HDR post");
    }
    chain.effects.reserve(prepared.effects.size());
    for (const PostEffect& effect : prepared.effects) {
        PostPass pass;
        pass.shader = load_shader(assets, fatal, effect.shader, "Post effect");
        pass.params = effect.params;
        pass.sample_depth = effect.sample_depth;
        pass.bloom = effect.shader == builtin::shader_post_bloom;
        chain.effects.push_back(std::move(pass));
    }
    return chain;
}

std::vector<PostStep> post_steps(bool hdr, int effect_count) {
    std::vector<PostStep> steps;
    if (effect_count < 0) {
        effect_count = 0;
    }
    PostImage source = PostImage::SceneA;
    const auto other = [](PostImage image) {
        return image == PostImage::SceneA ? PostImage::SceneB : PostImage::SceneA;
    };
    steps.reserve(static_cast<std::size_t>(effect_count) + (hdr ? 1u : 0u));
    for (int i = 0; i < effect_count; ++i) {
        const bool writes_window = i + 1 == effect_count && !hdr;
        PostStep step;
        step.effect = i;
        step.source = source;
        step.dest = writes_window ? PostImage::Window : other(source);
        steps.push_back(step);
        if (!writes_window) {
            source = step.dest;
        }
    }
    if (hdr) {
        PostStep step;
        step.tonemap = true;
        step.source = source;
        step.dest = PostImage::Window;
        steps.push_back(step);
    }
    return steps;
}

std::vector<FrameTarget> frame_targets(const CommandBuffer& commands) {
    std::vector<FrameTarget> targets;
    targets.reserve(commands.size());
    FrameTarget draw_target = FrameTarget::Window;
    for (const Command& command : commands) {
        if (const auto* view = std::get_if<CmdSetView>(&command)) {
            const bool scene = view->post.active() && !view->viewport.has_value();
            draw_target = scene ? FrameTarget::Scene : FrameTarget::Window;
            targets.push_back(FrameTarget::Window);
            continue;
        }
        if (std::holds_alternative<CmdPostProcess>(command)) {
            draw_target = FrameTarget::Window;
            targets.push_back(FrameTarget::Window);
            continue;
        }
        targets.push_back(draw_target);
    }
    return targets;
}

} // namespace engine::render
