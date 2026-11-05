#include "vulkan_backend.h"

#include "render/vulkan_clip.h"
#include "vulkan_mesh.h"

#include "render/slang_compiler.h"
#include "ui/painter.h"
#include "ui/render/paint_resources.h"

#include <engine/render/command_buffer.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace engine::render {

namespace {

constexpr VkDeviceSize kDefaultUboCapacity = 512u * 1024u;
constexpr VkDeviceSize kDefaultInstanceCapacity = 256u * 1024u;
constexpr std::uint32_t kMaxDescriptorsPerFrame = 2048u;

constexpr std::string_view kParticleSlang = R"(
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aInstancePos : TEXCOORD1;
    [[vk::location(3)]] float aInstanceRot : TEXCOORD2;
    [[vk::location(4)]] float2 aInstanceSize : TEXCOORD3;
    [[vk::location(5)]] float4 aInstanceColor : COLOR0;
    [[vk::location(6)]] float2 aInstanceUvScale : TEXCOORD4;
    [[vk::location(7)]] float2 aInstanceUvOffset : TEXCOORD5;
};
struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float4 vColor : COLOR0;
};
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float4 uColor;
uniform Sampler2D uTexture;
[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV * input.aInstanceUvScale + input.aInstanceUvOffset;
    out.vColor = input.aInstanceColor * uColor;
    float cos_r = cos(input.aInstanceRot);
    float sin_r = sin(input.aInstanceRot);
    float2 scaled = input.aPosition.xy * input.aInstanceSize;
    float2 rotated = float2(
        scaled.x * cos_r - scaled.y * sin_r,
        scaled.x * sin_r + scaled.y * cos_r);
    float3 world_pos = input.aInstancePos + float3(rotated, input.aPosition.z);
    out.position = mul(uProjection, mul(uView, float4(world_pos, 1.0)));
    return out;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return uTexture.Sample(input.vUV) * input.vColor;
}
)";

struct QuadVertex {
    float x, y, z;
    float u, v;
};

glm::mat3 normal_matrix(const glm::mat4& model) {
    const glm::mat3 upper(model);
    const float det = glm::determinant(upper);
    if (!std::isfinite(det) || std::abs(det) <= 1e-8f) {
        return glm::mat3(1.0f);
    }
    return glm::transpose(glm::inverse(upper));
}

void write_bytes(
    std::vector<std::uint8_t>& buf,
    std::uint32_t offset,
    const void* src,
    std::uint32_t size) {
    if (offset + size <= buf.size()) {
        std::memcpy(buf.data() + offset, src, size);
    }
}

void write_mat4(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    const glm::mat4& m) {
    const SpirvUniformMember* mem = refl.find_uniform(name);
    if (!mem) {
        return;
    }
    const glm::mat4 out = mem->is_row_major ? glm::transpose(m) : m;
    write_bytes(buf, mem->offset, &out[0][0], sizeof(glm::mat4));
}

void write_mat3(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    const glm::mat3& m) {
    const SpirvUniformMember* mem = refl.find_uniform(name);
    if (!mem) {
        return;
    }
    const glm::mat3 out = mem->is_row_major ? glm::transpose(m) : m;
    const std::uint32_t stride = mem->matrix_stride > 0 ? mem->matrix_stride : 16u;
    for (std::uint32_t c = 0; c < 3; ++c) {
        write_bytes(buf, mem->offset + c * stride, &out[static_cast<int>(c)][0], sizeof(glm::vec3));
    }
}

void write_vec4(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    const glm::vec4& v) {
    if (const SpirvUniformMember* mem = refl.find_uniform(name)) {
        write_bytes(buf, mem->offset, &v[0], sizeof(glm::vec4));
    }
}

void write_vec3(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    const glm::vec3& v) {
    if (const SpirvUniformMember* mem = refl.find_uniform(name)) {
        write_bytes(buf, mem->offset, &v[0], sizeof(glm::vec3));
    }
}

void write_vec2(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    const glm::vec2& v) {
    if (const SpirvUniformMember* mem = refl.find_uniform(name)) {
        write_bytes(buf, mem->offset, &v[0], sizeof(glm::vec2));
    }
}

void write_float(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    float v) {
    if (const SpirvUniformMember* mem = refl.find_uniform(name)) {
        write_bytes(buf, mem->offset, &v, sizeof(float));
    }
}

void write_int(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    int v) {
    if (const SpirvUniformMember* mem = refl.find_uniform(name)) {
        write_bytes(buf, mem->offset, &v, sizeof(int));
    }
}

void write_mat4_array(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    std::span<const glm::mat4> values) {
    const SpirvUniformMember* mem = refl.find_uniform(name);
    if (!mem || values.empty()) {
        return;
    }
    const std::uint32_t stride = mem->array_stride > 0 ? mem->array_stride : static_cast<std::uint32_t>(sizeof(glm::mat4));
    const std::uint32_t count = std::min(
        static_cast<std::uint32_t>(values.size()),
        mem->array_elements > 0 ? mem->array_elements : static_cast<std::uint32_t>(values.size()));
    for (std::uint32_t i = 0; i < count; ++i) {
        const glm::mat4 out = mem->is_row_major ? glm::transpose(values[i]) : values[i];
        write_bytes(buf, mem->offset + i * stride, &out[0][0], sizeof(glm::mat4));
    }
}

void write_vec3_array(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    std::string_view name,
    std::span<const glm::vec3> values) {
    const SpirvUniformMember* mem = refl.find_uniform(name);
    if (!mem) {
        return;
    }
    const std::uint32_t stride = mem->array_stride > 0 ? mem->array_stride : 16u;
    const std::uint32_t count = std::min(
        static_cast<std::uint32_t>(values.size()),
        mem->array_elements > 0 ? mem->array_elements : static_cast<std::uint32_t>(values.size()));
    for (std::uint32_t i = 0; i < count; ++i) {
        write_bytes(buf, mem->offset + i * stride, &values[i][0], sizeof(glm::vec3));
    }
}

void write_view_uniforms(
    std::vector<std::uint8_t>& buf,
    const SpirvReflection& refl,
    const CmdSetView& view) {
    write_mat4(buf, refl, "uView", view.view);
    // CPU projection stays OpenGL clip. The shader's uProjection is the same matrix with z in [0, w].
    write_mat4(buf, refl, "uProjection", vulkan_projection(view.projection));
    write_vec3(buf, refl, "uCameraPosition", view.camera_position);
    write_float(buf, refl, "uTime", view.time);
    const int count = std::clamp(
        view.lights.directional_count, 0, static_cast<int>(kMaxDirectionalLights));
    write_int(buf, refl, "uLightCount", count);
    write_vec3_array(buf, refl, "uLightDirections", view.lights.directions);
    write_vec3_array(buf, refl, "uLightColors", view.lights.colors);
    write_vec3(buf, refl, "uAmbient", view.lights.ambient);
    // Zero is off for every fog uniform, as in a zero-initialized UBO.
    write_int(buf, refl, "uFogEnabled", view.fog.has_value() ? 1 : 0);
    const Fog fog = view.fog.value_or(Fog{});
    write_vec3(buf, refl, "uFogColor", fog.color);
    write_vec2(buf, refl, "uFogRange", glm::vec2{fog.start, fog.end});
}

struct ExecuteVisitor {
    ui::IUiPainter* painter = nullptr;
    VulkanRenderBackend* backend = nullptr;

    void operator()(const CmdDrawMesh& cmd) const { backend->execute_draw_mesh(cmd); }

    void operator()(const CmdDrawUI& cmd) const {
        if (painter == nullptr || cmd.document == nullptr) {
            return;
        }
        ui::paint_document(
            *cmd.document,
            cmd.stylesheet,
            *painter,
            ui::UiPaintInput{
                .canvas_rect = cmd.rect,
                .pointer = cmd.pointer,
                .pointer_down = cmd.pointer_down,
                .delta_time = cmd.delta_time,
                .window_width = cmd.window_width,
                .window_height = cmd.window_height,
                .ui_offset = cmd.ui_offset,
                .ui_scale = cmd.ui_scale,
                .inspector_hover = cmd.inspector_hover,
                .inspector_selection = cmd.inspector_selection,
                .inspector_selection_path = cmd.inspector_selection_path,
                .inspector_selection_owner = cmd.inspector_selection_owner,
                .canvas = cmd.canvas,
                .popup_bounds = cmd.popup_bounds,
                .popup_layer = cmd.popup_layer,
            });
    }

    void operator()(const CmdDrawParticles& cmd) const { backend->execute_draw_particles(cmd); }

    void operator()(const CmdSetView& cmd) const { backend->set_view(cmd); }
};

} // namespace

VulkanRenderBackend::VulkanRenderBackend(std::shared_ptr<VulkanContext> context)
    : context_(std::move(context))
    , ui_resources_(std::make_unique<ui::PaintResources>()) {}

VulkanRenderBackend::~VulkanRenderBackend() {
    shutdown_gpu_resources();
}

void VulkanRenderBackend::begin_scene_pass(
    VkCommandBuffer cmd,
    VkRenderPass render_pass,
    VkExtent2D extent,
    std::uint32_t frame_slot) {
    active_cmd_ = cmd;
    active_render_pass_ = render_pass;
    pass_extent_ = extent;
    active_slot_ = frame_slot % kMaxFrameSlots;

    if (context_ && context_->is_device_ready() && ensure_frame_slot(active_slot_)) {
        FrameSlot& slot = frame_slots_[active_slot_];
        slot.ubo_offset = 0;
        slot.instance_offset = 0;
        context_->fn().vkResetDescriptorPool(context_->device(), slot.descriptor_pool, 0);
    }
}

void VulkanRenderBackend::end_scene_pass() noexcept {
    active_cmd_ = VK_NULL_HANDLE;
    active_render_pass_ = VK_NULL_HANDLE;
}

void VulkanRenderBackend::set_view(const CmdSetView& view) {
    view_ = view;
    if (active_cmd_ != VK_NULL_HANDLE) {
        apply_viewport_and_scissor(true);
    }
}

void VulkanRenderBackend::apply_viewport_and_scissor(bool clear_if_requested) {
    if (active_cmd_ == VK_NULL_HANDLE || pass_extent_.width == 0 || pass_extent_.height == 0) {
        return;
    }
    const VulkanFunctions& fn = context_->fn();

    float rx = 0.0f;
    float ry = 0.0f;
    float rw = static_cast<float>(pass_extent_.width);
    float rh = static_cast<float>(pass_extent_.height);

    if (view_.viewport.has_value()) {
        rx = std::max(0.0f, view_.viewport->x);
        ry = std::max(0.0f, view_.viewport->y);
        rw = std::max(1.0f, std::min(view_.viewport->w, static_cast<float>(pass_extent_.width) - rx));
        rh = std::max(1.0f, std::min(view_.viewport->h, static_cast<float>(pass_extent_.height) - ry));
    }

    // Negative height (Vulkan 1.1+ / maintenance1) flips clip-space Y to match OpenGL (+Y up).
    VkViewport vp{};
    vp.x = rx;
    vp.y = ry + rh;
    vp.width = rw;
    vp.height = -rh;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    fn.vkCmdSetViewport(active_cmd_, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {static_cast<std::int32_t>(rx), static_cast<std::int32_t>(ry)};
    sc.extent = {static_cast<std::uint32_t>(rw), static_cast<std::uint32_t>(rh)};
    fn.vkCmdSetScissor(active_cmd_, 0, 1, &sc);

    if (clear_if_requested && view_.clear_color.has_value() && fn.vkCmdClearAttachments) {
        std::array<VkClearAttachment, 2> atts{};
        atts[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        atts[0].colorAttachment = 0;
        atts[0].clearValue.color = {
            {view_.clear_color->r, view_.clear_color->g, view_.clear_color->b, view_.clear_color->a}};
        atts[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        atts[1].clearValue.depthStencil = {1.0f, 0};

        VkClearRect clear_rect{};
        clear_rect.rect = sc;
        clear_rect.baseArrayLayer = 0;
        clear_rect.layerCount = 1;
        fn.vkCmdClearAttachments(
            active_cmd_, static_cast<std::uint32_t>(atts.size()), atts.data(), 1, &clear_rect);
    }
}

void VulkanRenderBackend::execute_draw_mesh(const CmdDrawMesh& cmd) {
    if (active_cmd_ == VK_NULL_HANDLE || !cmd.mesh || !cmd.material || !context_ ||
        !context_->is_device_ready()) {
        return;
    }

    const auto shader = std::dynamic_pointer_cast<VulkanShader>(cmd.material->shader());
    const auto mesh = std::dynamic_pointer_cast<VulkanMesh>(cmd.mesh);
    if (!shader || !mesh || !shader->valid() || !mesh->valid() || !shader->ensure_modules()) {
        return;
    }

    const MaterialOverride* override_ptr =
        cmd.material_override.has_value() ? &*cmd.material_override : nullptr;
    VulkanTexture* albedo = resolve_albedo(*cmd.material, override_ptr, shader->reflection().sampler_arrayed);
    if (albedo == nullptr || !albedo->ensure_gpu_image()) {
        return;
    }

    const VkPipeline pipeline =
        get_or_create_pipeline(*shader, cmd.material->render_state(), false);
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    const SpirvReflection& refl = shader->reflection();
    std::vector<std::uint8_t> ubo(std::max<std::uint32_t>(refl.ubo_size, 64u), 0);
    write_mat4(ubo, refl, "uModel", cmd.model);
    write_mat3(ubo, refl, "uNormalMatrix", normal_matrix(cmd.model));
    write_view_uniforms(ubo, refl, view_);
    write_vec4(ubo, refl, "uColor", cmd.material->color() * cmd.color);
    write_vec2(ubo, refl, "uUvScale", cmd.uv_scale);
    write_vec2(ubo, refl, "uUvOffset", cmd.uv_offset);
    // No cutoff is 0: alpha is never below it, so nothing is discarded.
    write_float(ubo, refl, "uAlphaCutoff", cmd.material->render_state().alpha_cutoff.value_or(0.0f));
    if (cmd.bone_count > 0 && cmd.bones != nullptr) {
        write_mat4_array(ubo, refl, "uBones", std::span<const glm::mat4>(cmd.bones, static_cast<std::size_t>(cmd.bone_count)));
    }

    if (override_ptr != nullptr) {
        for (const auto& [param_name, param_val] : override_ptr->vec4_params) {
            write_vec4(ubo, refl, param_name, param_val);
        }
    }

    const VkDescriptorSet ds = allocate_and_write_descriptor(ubo, *albedo);
    if (ds == VK_NULL_HANDLE) {
        return;
    }

    const VulkanFunctions& fn = context_->fn();
    fn.vkCmdBindPipeline(active_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    fn.vkCmdBindDescriptorSets(
        active_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &ds, 0, nullptr);
    mesh->draw(active_cmd_);
}

void VulkanRenderBackend::execute_draw_particles(const CmdDrawParticles& cmd) {
    if (active_cmd_ == VK_NULL_HANDLE || cmd.instances.empty() || !cmd.material || !context_ ||
        !context_->is_device_ready()) {
        return;
    }
    if (!ensure_particle_resources() || !ensure_frame_slot(active_slot_)) {
        return;
    }

    // The builtin particle shader samples a 2D texture.
    VulkanTexture* albedo = resolve_albedo(*cmd.material, nullptr, false);
    if (albedo == nullptr || !albedo->ensure_gpu_image()) {
        return;
    }

    const RenderState state{
        .blend = cmd.blend,
        .depth_test = false,
        .depth_write = false,
        .cull = CullMode::None,
    };
    const VkPipeline pipeline = get_or_create_pipeline(*particle_shader_, state, true);
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    FrameSlot& slot = frame_slots_[active_slot_];
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(cmd.instances.size() * sizeof(ParticleInstance));
    const VkDeviceSize instance_offset = (slot.instance_offset + 15u) & ~VkDeviceSize{15u};
    if (instance_offset + bytes > slot.instance_capacity || slot.instance_mapped == nullptr) {
        return;
    }
    std::memcpy(slot.instance_mapped + instance_offset, cmd.instances.data(), static_cast<std::size_t>(bytes));
    slot.instance_offset = instance_offset + bytes;

    const SpirvReflection& refl = particle_shader_->reflection();
    std::vector<std::uint8_t> ubo(std::max<std::uint32_t>(refl.ubo_size, 64u), 0);
    write_view_uniforms(ubo, refl, view_);
    write_vec4(ubo, refl, "uColor", cmd.material->color());

    const VkDescriptorSet ds = allocate_and_write_descriptor(ubo, *albedo);
    if (ds == VK_NULL_HANDLE) {
        return;
    }

    const VulkanFunctions& fn = context_->fn();
    fn.vkCmdBindPipeline(active_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    fn.vkCmdBindDescriptorSets(
        active_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &ds, 0, nullptr);

    const std::array<VkBuffer, 2> vbs{particle_quad_buffer_, slot.instance_buffer};
    const std::array<VkDeviceSize, 2> offsets{0, instance_offset};
    fn.vkCmdBindVertexBuffers(active_cmd_, 0, 2, vbs.data(), offsets.data());
    fn.vkCmdDraw(active_cmd_, 6, static_cast<std::uint32_t>(cmd.instances.size()), 0, 0);
}

void VulkanRenderBackend::execute(const CommandBuffer& commands) {
    view_ = CmdSetView{};
    if (active_cmd_ != VK_NULL_HANDLE) {
        apply_viewport_and_scissor(false);
    }
    commands.execute(ExecuteVisitor{.painter = ui_painter_, .backend = this});
    if (active_cmd_ != VK_NULL_HANDLE) {
        view_.viewport = std::nullopt;
        apply_viewport_and_scissor(false);
    }
}

bool VulkanRenderBackend::ensure_shared_objects() {
    if (descriptor_set_layout_ != VK_NULL_HANDLE && pipeline_layout_ != VK_NULL_HANDLE) {
        return true;
    }
    if (!context_ || !context_->is_device_ready()) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_info{};
    dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    dsl_info.pBindings = bindings.data();
    if (fn.vkCreateDescriptorSetLayout(device, &dsl_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        descriptor_set_layout_ = VK_NULL_HANDLE;
        return false;
    }

    VkPipelineLayoutCreateInfo pl_info{};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 1;
    pl_info.pSetLayouts = &descriptor_set_layout_;
    if (fn.vkCreatePipelineLayout(device, &pl_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        fn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
        pipeline_layout_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanRenderBackend::ensure_frame_slot(std::uint32_t slot_idx) {
    FrameSlot& slot = frame_slots_[slot_idx];
    if (slot.descriptor_pool != VK_NULL_HANDLE && slot.ubo_buffer != VK_NULL_HANDLE &&
        slot.instance_buffer != VK_NULL_HANDLE) {
        return true;
    }
    if (!context_ || !context_->is_device_ready()) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorsPerFrame};
    pool_sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorsPerFrame};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = kMaxDescriptorsPerFrame;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    if (fn.vkCreateDescriptorPool(device, &pool_info, nullptr, &slot.descriptor_pool) != VK_SUCCESS) {
        slot.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    if (!context_->create_buffer(
            kDefaultUboCapacity,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            slot.ubo_buffer,
            slot.ubo_memory)) {
        return false;
    }
    void* ubo_ptr = nullptr;
    if (fn.vkMapMemory(device, slot.ubo_memory, 0, kDefaultUboCapacity, 0, &ubo_ptr) != VK_SUCCESS) {
        return false;
    }
    slot.ubo_mapped = static_cast<std::uint8_t*>(ubo_ptr);
    slot.ubo_capacity = kDefaultUboCapacity;

    if (!context_->create_buffer(
            kDefaultInstanceCapacity,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            slot.instance_buffer,
            slot.instance_memory)) {
        return false;
    }
    void* inst_ptr = nullptr;
    if (fn.vkMapMemory(device, slot.instance_memory, 0, kDefaultInstanceCapacity, 0, &inst_ptr) !=
        VK_SUCCESS) {
        return false;
    }
    slot.instance_mapped = static_cast<std::uint8_t*>(inst_ptr);
    slot.instance_capacity = kDefaultInstanceCapacity;
    return true;
}

bool VulkanRenderBackend::ensure_particle_resources() {
    if (particle_shader_ && particle_quad_buffer_ != VK_NULL_HANDLE) {
        return particle_shader_->ensure_modules();
    }
    const auto compiled = compile_slang_shader(kParticleSlang);
    if (!compiled.has_value()) {
        return false;
    }
    particle_shader_ = std::make_shared<VulkanShader>(context_, *compiled);
    if (!particle_shader_->valid() || !particle_shader_->ensure_modules()) {
        particle_shader_.reset();
        return false;
    }

    static constexpr QuadVertex kQuadVertices[6] = {
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f, 0.0f, 1.0f, 1.0f},
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f, 1.0f, 1.0f},
        {-0.5f, 0.5f, 0.0f, 0.0f, 1.0f},
    };
    return context_->upload_host_buffer(
        sizeof(kQuadVertices),
        kQuadVertices,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        particle_quad_buffer_,
        particle_quad_memory_);
}

VulkanTexture* VulkanRenderBackend::resolve_albedo(
    const IMaterial& material,
    const MaterialOverride* override_params,
    bool arrayed) {
    VulkanTexture* chosen = nullptr;
    if (override_params != nullptr && override_params->albedo != nullptr) {
        if (auto* tex = dynamic_cast<VulkanTexture*>(override_params->albedo.get());
            tex != nullptr && tex->valid()) {
            chosen = tex;
        }
    }
    if (chosen == nullptr) {
        if (const auto mat_tex = material.texture(0)) {
            if (auto* tex = dynamic_cast<VulkanTexture*>(mat_tex.get()); tex != nullptr && tex->valid()) {
                chosen = tex;
            }
        }
    }
    if (chosen != nullptr && (chosen->layers() > 0) == arrayed) {
        return chosen;
    }
    if (arrayed) {
        if (!white_texture_array_) {
            const TextureArrayDesc white_desc{
                .width = 1,
                .height = 1,
                .layers = 1,
                .rgba = {255, 255, 255, 255},
                .filter = FilterMode::Nearest,
                .wrap = WrapMode::Clamp,
            };
            white_texture_array_ = std::make_shared<VulkanTexture>(context_, white_desc);
        }
        return white_texture_array_.get();
    }
    if (!white_texture_) {
        TextureDesc white_desc{
            .width = 1,
            .height = 1,
            .rgba = {255, 255, 255, 255},
            .filter = FilterMode::Nearest,
            .wrap = WrapMode::Clamp,
        };
        white_texture_ = std::make_shared<VulkanTexture>(context_, white_desc);
    }
    return white_texture_.get();
}

VkDescriptorSet VulkanRenderBackend::allocate_and_write_descriptor(
    const std::vector<std::uint8_t>& ubo_bytes,
    VulkanTexture& texture) {
    if (!ensure_shared_objects() || !ensure_frame_slot(active_slot_)) {
        return VK_NULL_HANDLE;
    }
    FrameSlot& slot = frame_slots_[active_slot_];
    const VkDeviceSize align = std::max<VkDeviceSize>(
        context_->device_properties().limits.minUniformBufferOffsetAlignment, 16u);
    const VkDeviceSize aligned_offset = (slot.ubo_offset + align - 1u) & ~(align - 1u);
    const VkDeviceSize size = static_cast<VkDeviceSize>(ubo_bytes.size());
    if (aligned_offset + size > slot.ubo_capacity || slot.ubo_mapped == nullptr) {
        return VK_NULL_HANDLE;
    }

    std::memcpy(slot.ubo_mapped + aligned_offset, ubo_bytes.data(), ubo_bytes.size());
    slot.ubo_offset = aligned_offset + size;

    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = slot.descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_set_layout_;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (fn.vkAllocateDescriptorSets(device, &alloc_info, &ds) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = slot.ubo_buffer;
    buf_info.offset = aligned_offset;
    buf_info.range = size;

    VkDescriptorImageInfo img_info{};
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_info.imageView = texture.image_view();
    img_info.sampler = texture.sampler();

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &buf_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &img_info;

    fn.vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return ds;
}

VkPipeline VulkanRenderBackend::get_or_create_pipeline(
    VulkanShader& shader,
    const RenderState& state,
    bool is_particle) {
    if (!ensure_shared_objects() || active_render_pass_ == VK_NULL_HANDLE || !shader.ensure_modules()) {
        return VK_NULL_HANDLE;
    }

    // The cutoff is a uniform, not pipeline state: one pipeline serves every cutoff value.
    RenderState pipeline_state = state;
    pipeline_state.alpha_cutoff.reset();
    const PipelineKey key{
        .vert = shader.vertex_module(),
        .frag = shader.fragment_module(),
        .state = pipeline_state,
        .render_pass = active_render_pass_,
        .is_particle = is_particle,
    };
    for (const CachedPipeline& entry : pipelines_) {
        if (entry.key == key) {
            return entry.pipeline;
        }
    }

    const SpirvReflection& refl = shader.reflection();
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader.vertex_module();
    stages[0].pName = refl.vertex_entry.c_str();

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader.fragment_module();
    stages[1].pName = refl.fragment_entry.c_str();

    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attrs;
    if (!is_particle) {
        vertex_bindings.push_back({0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX});
        vertex_attrs.push_back(
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, position))});
        vertex_attrs.push_back(
            {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, uv))});
        vertex_attrs.push_back(
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, normal))});
        vertex_attrs.push_back(
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, color))});
        vertex_attrs.push_back(
            {4, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, layer))});
        if (refl.find_uniform("uBones") != nullptr) {
            vertex_bindings.push_back({1, sizeof(SkinInfluence), VK_VERTEX_INPUT_RATE_VERTEX});
            vertex_attrs.push_back(
                    {5, 1, VK_FORMAT_R8G8B8A8_UINT, static_cast<std::uint32_t>(offsetof(SkinInfluence, joints))});
            vertex_attrs.push_back(
                {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(SkinInfluence, weights))});
        }
    } else {
        vertex_bindings.push_back({0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX});
        vertex_bindings.push_back({1, sizeof(ParticleInstance), VK_VERTEX_INPUT_RATE_INSTANCE});
        vertex_attrs.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
        vertex_attrs.push_back({1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 3});
        vertex_attrs.push_back({2, 1, VK_FORMAT_R32G32B32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, position))});
        vertex_attrs.push_back({3, 1, VK_FORMAT_R32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, rotation))});
        vertex_attrs.push_back({4, 1, VK_FORMAT_R32G32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, size))});
        vertex_attrs.push_back({5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, color))});
        vertex_attrs.push_back({6, 1, VK_FORMAT_R32G32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, uv_scale))});
        vertex_attrs.push_back({7, 1, VK_FORMAT_R32G32_SFLOAT,
            static_cast<std::uint32_t>(offsetof(ParticleInstance, uv_offset))});
    }

    VkPipelineVertexInputStateCreateInfo vi_info{};
    vi_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_info.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertex_bindings.size());
    vi_info.pVertexBindingDescriptions = vertex_bindings.data();
    vi_info.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertex_attrs.size());
    vi_info.pVertexAttributeDescriptions = vertex_attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia_info{};
    ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp_info{};
    vp_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_info.viewportCount = 1;
    vp_info.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs_info{};
    rs_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs_info.polygonMode = VK_POLYGON_MODE_FILL;
    rs_info.lineWidth = 1.0f;
    rs_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs_info.cullMode = state.cull == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                       : state.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                       : VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo ms_info{};
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds_info{};
    ds_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds_info.depthTestEnable = (state.depth_test || state.depth_write) ? VK_TRUE : VK_FALSE;
    ds_info.depthWriteEnable = state.depth_write ? VK_TRUE : VK_FALSE;
    ds_info.depthCompareOp = state.depth_test ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (state.blend == BlendMode::Alpha) {
        cb_att.blendEnable = VK_TRUE;
        cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb_att.colorBlendOp = VK_BLEND_OP_ADD;
        cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb_att.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (state.blend == BlendMode::Additive) {
        cb_att.blendEnable = VK_TRUE;
        cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cb_att.colorBlendOp = VK_BLEND_OP_ADD;
        cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb_att.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_info.attachmentCount = 1;
    cb_info.pAttachments = &cb_att;

    constexpr std::array<VkDynamicState, 2> kDynStates{
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_info{};
    dyn_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_info.dynamicStateCount = static_cast<std::uint32_t>(kDynStates.size());
    dyn_info.pDynamicStates = kDynStates.data();

    VkGraphicsPipelineCreateInfo gp_info{};
    gp_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_info.stageCount = static_cast<std::uint32_t>(stages.size());
    gp_info.pStages = stages.data();
    gp_info.pVertexInputState = &vi_info;
    gp_info.pInputAssemblyState = &ia_info;
    gp_info.pViewportState = &vp_info;
    gp_info.pRasterizationState = &rs_info;
    gp_info.pMultisampleState = &ms_info;
    gp_info.pDepthStencilState = &ds_info;
    gp_info.pColorBlendState = &cb_info;
    gp_info.pDynamicState = &dyn_info;
    gp_info.layout = pipeline_layout_;
    gp_info.renderPass = active_render_pass_;
    gp_info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (context_->fn().vkCreateGraphicsPipelines(
            context_->device(), VK_NULL_HANDLE, 1, &gp_info, nullptr, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    pipelines_.push_back(CachedPipeline{.key = key, .pipeline = pipeline});
    return pipeline;
}

void VulkanRenderBackend::shutdown_gpu_resources() {
    white_texture_.reset();
    white_texture_array_.reset();
    particle_shader_.reset();

    if (!context_ || !context_->is_device_ready()) {
        pipelines_.clear();
        pipeline_layout_ = VK_NULL_HANDLE;
        descriptor_set_layout_ = VK_NULL_HANDLE;
        return;
    }

    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();

    for (CachedPipeline& entry : pipelines_) {
        if (entry.pipeline != VK_NULL_HANDLE && fn.vkDestroyPipeline) {
            fn.vkDestroyPipeline(device, entry.pipeline, nullptr);
        }
    }
    pipelines_.clear();

    if (particle_quad_buffer_ != VK_NULL_HANDLE) {
        fn.vkDestroyBuffer(device, particle_quad_buffer_, nullptr);
        particle_quad_buffer_ = VK_NULL_HANDLE;
    }
    if (particle_quad_memory_ != VK_NULL_HANDLE) {
        fn.vkFreeMemory(device, particle_quad_memory_, nullptr);
        particle_quad_memory_ = VK_NULL_HANDLE;
    }

    for (FrameSlot& slot : frame_slots_) {
        if (slot.ubo_mapped != nullptr) {
            fn.vkUnmapMemory(device, slot.ubo_memory);
            slot.ubo_mapped = nullptr;
        }
        if (slot.ubo_buffer != VK_NULL_HANDLE) {
            fn.vkDestroyBuffer(device, slot.ubo_buffer, nullptr);
            slot.ubo_buffer = VK_NULL_HANDLE;
        }
        if (slot.ubo_memory != VK_NULL_HANDLE) {
            fn.vkFreeMemory(device, slot.ubo_memory, nullptr);
            slot.ubo_memory = VK_NULL_HANDLE;
        }
        if (slot.instance_mapped != nullptr) {
            fn.vkUnmapMemory(device, slot.instance_memory);
            slot.instance_mapped = nullptr;
        }
        if (slot.instance_buffer != VK_NULL_HANDLE) {
            fn.vkDestroyBuffer(device, slot.instance_buffer, nullptr);
            slot.instance_buffer = VK_NULL_HANDLE;
        }
        if (slot.instance_memory != VK_NULL_HANDLE) {
            fn.vkFreeMemory(device, slot.instance_memory, nullptr);
            slot.instance_memory = VK_NULL_HANDLE;
        }
        if (slot.descriptor_pool != VK_NULL_HANDLE && fn.vkDestroyDescriptorPool) {
            fn.vkDestroyDescriptorPool(device, slot.descriptor_pool, nullptr);
            slot.descriptor_pool = VK_NULL_HANDLE;
        }
    }

    if (pipeline_layout_ != VK_NULL_HANDLE && fn.vkDestroyPipelineLayout) {
        fn.vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE && fn.vkDestroyDescriptorSetLayout) {
        fn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::render
