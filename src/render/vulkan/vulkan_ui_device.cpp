#include "vulkan_ui_device.h"

#include "ui_spirv.inc"
#include "ui/render/ui_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace engine::render {
namespace {

constexpr std::uint32_t kMaxUiDescriptorSetsPerFrame = 256;
constexpr VkDeviceSize kUiUboSize = 16;

std::size_t grown_capacity(std::size_t current, std::size_t needed) {
    if (needed <= current) {
        return current;
    }
    const std::size_t grown = current + current / 2;
    return grown > needed ? grown : needed;
}

VkFormat vk_vertex_format(ui::UiVertexFormat format) {
    switch (format) {
    case ui::UiVertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case ui::UiVertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ui::UiVertexFormat::UNorm8x4:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case ui::UiVertexFormat::UInt:
        return VK_FORMAT_R32_UINT;
    }
    return VK_FORMAT_R32G32_SFLOAT;
}

VkBlendFactor vk_blend_factor(ui::UiBlendFactor factor) {
    switch (factor) {
    case ui::UiBlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case ui::UiBlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

void record_image_barrier(
    const VulkanFunctions& fn,
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags src_access,
    VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;

    fn.vkCmdPipelineBarrier(
        cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void destroy_host_buffer(const VulkanContext& ctx, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (!ctx.is_device_ready()) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice dev = ctx.device();
    if (buffer != VK_NULL_HANDLE && fn.vkDestroyBuffer) {
        fn.vkDestroyBuffer(dev, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE && fn.vkFreeMemory) {
        fn.vkFreeMemory(dev, memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

bool write_host_memory(
    const VulkanContext& ctx,
    VkDeviceMemory memory,
    const void* data,
    VkDeviceSize size) {
    if (!ctx.is_device_ready() || memory == VK_NULL_HANDLE || data == nullptr || size == 0) {
        return false;
    }
    const VulkanFunctions& fn = ctx.fn();
    const VkDevice dev = ctx.device();
    void* mapped = nullptr;
    if (!fn.vkMapMemory || fn.vkMapMemory(dev, memory, 0, size, 0, &mapped) != VK_SUCCESS ||
        mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    fn.vkUnmapMemory(dev, memory);
    return true;
}

} // namespace

VulkanUiDevice::VulkanUiDevice(std::shared_ptr<VulkanContext> context)
    : context_(std::move(context)) {
    bound_pages_.fill(ui::UiBatch::kUnbound);
}

VulkanUiDevice::~VulkanUiDevice() {
    shutdown();
}

bool VulkanUiDevice::create() {
    shutdown();
    ShaderDesc desc{};
    desc.vertex_spirv.assign(std::begin(kUiVertexSpirv), std::end(kUiVertexSpirv));
    desc.fragment_spirv.assign(std::begin(kUiFragmentSpirv), std::end(kUiFragmentSpirv));
    shader_ = std::make_unique<VulkanShader>(context_, desc);
    if (!shader_->valid()) {
        shader_.reset();
        return false;
    }
    if (context_ != nullptr && context_->is_device_ready()) {
        if (!ensure_gpu_objects()) {
            shutdown();
            return false;
        }
    }
    return true;
}

void VulkanUiDevice::shutdown() {
    release_textures();

    if (context_ != nullptr && context_->is_device_ready()) {
        const VulkanFunctions& fn = context_->fn();
        const VkDevice dev = context_->device();

        destroy_gpu_texture(fallback_texture_);

        for (FrameBuffers& fb : frame_buffers_) {
            if (fb.descriptor_pool != VK_NULL_HANDLE && fn.vkDestroyDescriptorPool) {
                fn.vkDestroyDescriptorPool(dev, fb.descriptor_pool, nullptr);
            }
            fb.descriptor_pool = VK_NULL_HANDLE;
            destroy_host_buffer(*context_, fb.ubo_buffer, fb.ubo_memory);
            destroy_host_buffer(*context_, fb.vertex_buffer, fb.vertex_memory);
            fb.vertex_capacity = 0;
            destroy_host_buffer(*context_, fb.index_buffer, fb.index_memory);
            fb.index_capacity = 0;
        }

        if (pipeline_ != VK_NULL_HANDLE && fn.vkDestroyPipeline) {
            fn.vkDestroyPipeline(dev, pipeline_, nullptr);
        }
        pipeline_ = VK_NULL_HANDLE;
        pipeline_render_pass_ = VK_NULL_HANDLE;

        if (pipeline_layout_ != VK_NULL_HANDLE && fn.vkDestroyPipelineLayout) {
            fn.vkDestroyPipelineLayout(dev, pipeline_layout_, nullptr);
        }
        pipeline_layout_ = VK_NULL_HANDLE;

        if (descriptor_set_layout_ != VK_NULL_HANDLE && fn.vkDestroyDescriptorSetLayout) {
            fn.vkDestroyDescriptorSetLayout(dev, descriptor_set_layout_, nullptr);
        }
        descriptor_set_layout_ = VK_NULL_HANDLE;

        if (sampler_ != VK_NULL_HANDLE && fn.vkDestroySampler) {
            fn.vkDestroySampler(dev, sampler_, nullptr);
        }
        sampler_ = VK_NULL_HANDLE;
    }

    shader_.reset();
    active_cmd_ = VK_NULL_HANDLE;
    active_render_pass_ = VK_NULL_HANDLE;
}

void VulkanUiDevice::set_frame_context(
    VkCommandBuffer cmd,
    VkRenderPass render_pass,
    std::uint32_t frame_index) noexcept {
    active_cmd_ = cmd;
    active_render_pass_ = render_pass;
    active_frame_ = frame_index % kMaxFramesInFlight;
}

bool VulkanUiDevice::ensure_gpu_objects() {
    if (context_ == nullptr || !context_->is_device_ready()) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice dev = context_->device();

    if (sampler_ == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samp_info{};
        samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp_info.magFilter = VK_FILTER_LINEAR;
        samp_info.minFilter = VK_FILTER_LINEAR;
        samp_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.maxAnisotropy = 1.0f;
        samp_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        if (!fn.vkCreateSampler ||
            fn.vkCreateSampler(dev, &samp_info, nullptr, &sampler_) != VK_SUCCESS) {
            sampler_ = VK_NULL_HANDLE;
            return false;
        }
    }

    if (descriptor_set_layout_ == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 1 + ui::kUiTextureSlotCount> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        for (std::uint32_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
            bindings[1 + slot].binding = 1 + slot;
            bindings[1 + slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[1 + slot].descriptorCount = 1;
            bindings[1 + slot].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo dsl_info{};
        dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        dsl_info.pBindings = bindings.data();
        if (!fn.vkCreateDescriptorSetLayout ||
            fn.vkCreateDescriptorSetLayout(dev, &dsl_info, nullptr, &descriptor_set_layout_) !=
                VK_SUCCESS) {
            descriptor_set_layout_ = VK_NULL_HANDLE;
            return false;
        }
    }

    if (pipeline_layout_ == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo pl_info{};
        pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_info.setLayoutCount = 1;
        pl_info.pSetLayouts = &descriptor_set_layout_;
        if (!fn.vkCreatePipelineLayout ||
            fn.vkCreatePipelineLayout(dev, &pl_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
            pipeline_layout_ = VK_NULL_HANDLE;
            return false;
        }
    }

    if (fallback_texture_.view == VK_NULL_HANDLE) {
        if (!allocate_gpu_texture(fallback_texture_, 1, 1, ui::UiTextureFormat::Rgba8)) {
            return false;
        }
        const std::array<std::uint8_t, 4> zero_px{0, 0, 0, 0};
        ui::UiTextureRegion reg{};
        reg.pixels = zero_px;
        reg.row_texels = 1;
        reg.rect = {0, 0, 1, 1};
        upload_texture_region(fallback_texture_, reg);
    }

    return true;
}

bool VulkanUiDevice::ensure_pipeline(VkRenderPass render_pass) {
    if (render_pass == VK_NULL_HANDLE || shader_ == nullptr || !shader_->ensure_modules()) {
        return false;
    }
    if (pipeline_ != VK_NULL_HANDLE && pipeline_render_pass_ == render_pass) {
        return true;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice dev = context_->device();

    if (pipeline_ != VK_NULL_HANDLE && fn.vkDestroyPipeline) {
        fn.vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader_->vertex_module();
    stages[0].pName = shader_->reflection().vertex_entry.c_str();

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader_->fragment_module();
    stages[1].pName = shader_->reflection().fragment_entry.c_str();

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = ui::kUiVertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, ui::kUiVertexAttributes.size()> attrs{};
    for (std::size_t i = 0; i < ui::kUiVertexAttributes.size(); ++i) {
        attrs[i].binding = 0;
        attrs[i].location = ui::kUiVertexAttributes[i].location;
        attrs[i].format = vk_vertex_format(ui::kUiVertexAttributes[i].format);
        attrs[i].offset = ui::kUiVertexAttributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vi_info{};
    vi_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_info.vertexBindingDescriptionCount = 1;
    vi_info.pVertexBindingDescriptions = &binding;
    vi_info.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vi_info.pVertexAttributeDescriptions = attrs.data();

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
    rs_info.cullMode = VK_CULL_MODE_NONE;
    rs_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms_info{};
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds_info{};
    ds_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds_info.depthTestEnable = VK_FALSE;
    ds_info.depthWriteEnable = VK_FALSE;
    ds_info.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable = VK_TRUE;
    cb_att.srcColorBlendFactor = vk_blend_factor(ui::kUiBlend.src_color);
    cb_att.dstColorBlendFactor = vk_blend_factor(ui::kUiBlend.dst_color);
    cb_att.colorBlendOp = VK_BLEND_OP_ADD;
    cb_att.srcAlphaBlendFactor = vk_blend_factor(ui::kUiBlend.src_alpha);
    cb_att.dstAlphaBlendFactor = vk_blend_factor(ui::kUiBlend.dst_alpha);
    cb_att.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_info.attachmentCount = 1;
    cb_info.pAttachments = &cb_att;

    const std::array<VkDynamicState, 2> dyn_states{
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_info{};
    dyn_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_info.dynamicStateCount = static_cast<std::uint32_t>(dyn_states.size());
    dyn_info.pDynamicStates = dyn_states.data();

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
    gp_info.renderPass = render_pass;
    gp_info.subpass = 0;

    if (!fn.vkCreateGraphicsPipelines ||
        fn.vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp_info, nullptr, &pipeline_) !=
            VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        return false;
    }
    pipeline_render_pass_ = render_pass;
    return true;
}

bool VulkanUiDevice::ensure_frame_buffers(std::uint32_t frame_index) {
    FrameBuffers& fb = frame_buffers_[frame_index % kMaxFramesInFlight];
    const VulkanFunctions& fn = context_->fn();
    const VkDevice dev = context_->device();

    if (fb.descriptor_pool == VK_NULL_HANDLE) {
        std::array<VkDescriptorPoolSize, 2> sizes{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = kMaxUiDescriptorSetsPerFrame;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount =
            kMaxUiDescriptorSetsPerFrame * static_cast<std::uint32_t>(ui::kUiTextureSlotCount);

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = kMaxUiDescriptorSetsPerFrame;
        pool_info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        pool_info.pPoolSizes = sizes.data();
        if (!fn.vkCreateDescriptorPool ||
            fn.vkCreateDescriptorPool(dev, &pool_info, nullptr, &fb.descriptor_pool) != VK_SUCCESS) {
            fb.descriptor_pool = VK_NULL_HANDLE;
            return false;
        }
    }

    if (fb.ubo_buffer == VK_NULL_HANDLE) {
        if (!context_->create_buffer(
                kUiUboSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                fb.ubo_buffer,
                fb.ubo_memory)) {
            return false;
        }
    }
    return true;
}

bool VulkanUiDevice::allocate_gpu_texture(
    Texture& tex,
    int width,
    int height,
    ui::UiTextureFormat format) {
    if (context_ == nullptr || !context_->is_device_ready() || width <= 0 || height <= 0) {
        return false;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice dev = context_->device();
    const VkFormat vk_format =
        (format == ui::UiTextureFormat::R8) ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = {
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1u};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = vk_format;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!fn.vkCreateImage ||
        fn.vkCreateImage(dev, &img_info, nullptr, &tex.image) != VK_SUCCESS) {
        tex.image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    fn.vkGetImageMemoryRequirements(dev, tex.image, &mem_reqs);
    const std::optional<std::uint32_t> mem_idx = context_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!mem_idx.has_value()) {
        destroy_gpu_texture(tex);
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = *mem_idx;
    if (fn.vkAllocateMemory(dev, &alloc_info, nullptr, &tex.memory) != VK_SUCCESS) {
        destroy_gpu_texture(tex);
        return false;
    }
    if (fn.vkBindImageMemory(dev, tex.image, tex.memory, 0) != VK_SUCCESS) {
        destroy_gpu_texture(tex);
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = tex.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = vk_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    if (!fn.vkCreateImageView ||
        fn.vkCreateImageView(dev, &view_info, nullptr, &tex.view) != VK_SUCCESS) {
        destroy_gpu_texture(tex);
        return false;
    }

    VkCommandBuffer cmd = context_->begin_single_time_commands();
    if (cmd == VK_NULL_HANDLE) {
        destroy_gpu_texture(tex);
        return false;
    }
    record_image_barrier(
        fn,
        cmd,
        tex.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    context_->end_single_time_commands(cmd);
    return true;
}

void VulkanUiDevice::destroy_gpu_texture(Texture& tex) {
    if (context_ != nullptr && context_->is_device_ready()) {
        const VulkanFunctions& fn = context_->fn();
        const VkDevice dev = context_->device();
        if (tex.view != VK_NULL_HANDLE && fn.vkDestroyImageView) {
            fn.vkDestroyImageView(dev, tex.view, nullptr);
        }
        if (tex.image != VK_NULL_HANDLE && fn.vkDestroyImage) {
            fn.vkDestroyImage(dev, tex.image, nullptr);
        }
        if (tex.memory != VK_NULL_HANDLE && fn.vkFreeMemory) {
            fn.vkFreeMemory(dev, tex.memory, nullptr);
        }
    }
    tex = Texture{};
}

void VulkanUiDevice::upload_texture_region(Texture& tex, const ui::UiTextureRegion& texels) {
    const ui::AtlasRect& rect = texels.rect;
    if (rect.empty() || tex.image == VK_NULL_HANDLE || context_ == nullptr ||
        !context_->is_device_ready()) {
        return;
    }
    const int rect_w = rect.x1 - rect.x0;
    const int rect_h = rect.y1 - rect.y0;
    if (rect_w <= 0 || rect_h <= 0 || texels.row_texels < rect.x1) {
        return;
    }
    const std::size_t channels = (tex.format == ui::UiTextureFormat::R8) ? 1u : 4u;
    const std::size_t dst_row_bytes = static_cast<std::size_t>(rect_w) * channels;
    const std::size_t src_row_bytes = static_cast<std::size_t>(texels.row_texels) * channels;
    const std::size_t required_src =
        static_cast<std::size_t>(rect.y1 - 1) * src_row_bytes +
        static_cast<std::size_t>(rect.x1) * channels;
    if (texels.pixels.size() < required_src) {
        return;
    }

    std::vector<std::uint8_t> staging_bytes(dst_row_bytes * static_cast<std::size_t>(rect_h));
    for (int row = 0; row < rect_h; ++row) {
        const std::size_t src_offset =
            static_cast<std::size_t>(rect.y0 + row) * src_row_bytes +
            static_cast<std::size_t>(rect.x0) * channels;
        std::memcpy(
            staging_bytes.data() + static_cast<std::size_t>(row) * dst_row_bytes,
            texels.pixels.data() + src_offset,
            dst_row_bytes);
    }

    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!context_->upload_host_buffer(
            staging_bytes.size(),
            staging_bytes.data(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            staging_buf,
            staging_mem)) {
        return;
    }

    VkCommandBuffer cmd = context_->begin_single_time_commands();
    if (cmd == VK_NULL_HANDLE) {
        destroy_host_buffer(*context_, staging_buf, staging_mem);
        return;
    }

    const VulkanFunctions& fn = context_->fn();
    record_image_barrier(
        fn,
        cmd,
        tex.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {rect.x0, rect.y0, 0};
    region.imageExtent = {
        static_cast<std::uint32_t>(rect_w), static_cast<std::uint32_t>(rect_h), 1u};
    fn.vkCmdCopyBufferToImage(
        cmd, staging_buf, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    record_image_barrier(
        fn,
        cmd,
        tex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    context_->end_single_time_commands(cmd);
    destroy_host_buffer(*context_, staging_buf, staging_mem);
}

void VulkanUiDevice::begin_frame(float width, float height, float pixel_ratio) {
    draw_calls_ = 0;
    has_uploaded_geometry_ = false;
    bound_pages_.fill(ui::UiBatch::kUnbound);
    view_width_ = width;
    view_height_ = height;
    pixel_ratio_ = pixel_ratio;

    if (shader_ == nullptr || context_ == nullptr || !context_->is_device_ready() ||
        active_cmd_ == VK_NULL_HANDLE) {
        return;
    }
    if (!ensure_gpu_objects() || !ensure_frame_buffers(active_frame_) ||
        !ensure_pipeline(active_render_pass_)) {
        return;
    }

    FrameBuffers& fb = frame_buffers_[active_frame_];
    const VulkanFunctions& fn = context_->fn();
    fn.vkResetDescriptorPool(context_->device(), fb.descriptor_pool, 0);

    const std::array<float, 4> ubo_data{width, height, 0.0f, 0.0f};
    write_host_memory(*context_, fb.ubo_memory, ubo_data.data(), sizeof(ubo_data));
}

void VulkanUiDevice::create_texture(
    ui::UiTextureSlot slot,
    std::uint32_t page,
    int width,
    int height,
    ui::UiTextureFormat format,
    const ui::UiTextureRegion& texels) {
    std::vector<Texture>& slot_textures = textures_[static_cast<std::size_t>(slot)];
    if (slot_textures.size() <= page) {
        slot_textures.resize(static_cast<std::size_t>(page) + 1);
    }
    Texture& tex = slot_textures[page];
    destroy_gpu_texture(tex);
    tex.width = width;
    tex.height = height;
    tex.format = format;
    tex.valid = true;
    bound_pages_[static_cast<std::size_t>(slot)] = page;

    if (context_ != nullptr && context_->is_device_ready()) {
        if (allocate_gpu_texture(tex, width, height, format)) {
            upload_texture_region(tex, texels);
        }
    }
}

void VulkanUiDevice::update_texture(
    ui::UiTextureSlot slot,
    std::uint32_t page,
    const ui::UiTextureRegion& texels) {
    std::vector<Texture>& slot_textures = textures_[static_cast<std::size_t>(slot)];
    if (page >= slot_textures.size() || !slot_textures[page].valid) {
        return;
    }
    bound_pages_[static_cast<std::size_t>(slot)] = page;
    upload_texture_region(slot_textures[page], texels);
}

void VulkanUiDevice::release_textures() {
    for (std::vector<Texture>& slot_textures : textures_) {
        for (Texture& tex : slot_textures) {
            destroy_gpu_texture(tex);
        }
        slot_textures.clear();
    }
    bound_pages_.fill(ui::UiBatch::kUnbound);
}

void VulkanUiDevice::upload(
    std::span<const ui::UiVertex> vertices,
    std::span<const ui::UiIndex> indices) {
    if (shader_ == nullptr) {
        return;
    }
    has_uploaded_geometry_ = !vertices.empty() && !indices.empty();
    if (!has_uploaded_geometry_ || context_ == nullptr || !context_->is_device_ready() ||
        active_cmd_ == VK_NULL_HANDLE) {
        return;
    }

    FrameBuffers& fb = frame_buffers_[active_frame_];
    const std::size_t v_bytes = vertices.size_bytes();
    if (v_bytes > fb.vertex_capacity) {
        destroy_host_buffer(*context_, fb.vertex_buffer, fb.vertex_memory);
        fb.vertex_capacity = grown_capacity(fb.vertex_capacity, v_bytes);
        if (!context_->create_buffer(
                fb.vertex_capacity,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                fb.vertex_buffer,
                fb.vertex_memory)) {
            fb.vertex_capacity = 0;
            has_uploaded_geometry_ = false;
            return;
        }
    }
    write_host_memory(*context_, fb.vertex_memory, vertices.data(), v_bytes);

    const std::size_t i_bytes = indices.size_bytes();
    if (i_bytes > fb.index_capacity) {
        destroy_host_buffer(*context_, fb.index_buffer, fb.index_memory);
        fb.index_capacity = grown_capacity(fb.index_capacity, i_bytes);
        if (!context_->create_buffer(
                fb.index_capacity,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                fb.index_buffer,
                fb.index_memory)) {
            fb.index_capacity = 0;
            has_uploaded_geometry_ = false;
            return;
        }
    }
    write_host_memory(*context_, fb.index_memory, indices.data(), i_bytes);
}

void VulkanUiDevice::draw(std::span<const ui::UiBatch> batches) {
    if (shader_ == nullptr || !has_uploaded_geometry_) {
        return;
    }

    const bool gpu_active = context_ != nullptr && context_->is_device_ready() &&
                            active_cmd_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE;
    if (gpu_active) {
        const VulkanFunctions& fn = context_->fn();
        const FrameBuffers& fb = frame_buffers_[active_frame_];
        const float px_w = std::max(1.0f, std::round(view_width_ * pixel_ratio_));
        const float px_h = std::max(1.0f, std::round(view_height_ * pixel_ratio_));

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = px_h;
        vp.width = px_w;
        vp.height = -px_h;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        fn.vkCmdSetViewport(active_cmd_, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = {0, 0};
        sc.extent = {
            static_cast<std::uint32_t>(px_w), static_cast<std::uint32_t>(px_h)};
        fn.vkCmdSetScissor(active_cmd_, 0, 1, &sc);

        fn.vkCmdBindPipeline(active_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkDeviceSize v_offset = 0;
        fn.vkCmdBindVertexBuffers(active_cmd_, 0, 1, &fb.vertex_buffer, &v_offset);
        fn.vkCmdBindIndexBuffer(active_cmd_, fb.index_buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    for (const ui::UiBatch& batch : batches) {
        if (batch.index_count == 0) {
            continue;
        }
        for (std::size_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
            if (batch.pages[slot] != ui::UiBatch::kUnbound) {
                bound_pages_[slot] = batch.pages[slot];
            }
        }

        if (gpu_active) {
            const VulkanFunctions& fn = context_->fn();
            const VkDevice dev = context_->device();
            const FrameBuffers& fb = frame_buffers_[active_frame_];

            VkDescriptorSetAllocateInfo ds_alloc{};
            ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ds_alloc.descriptorPool = fb.descriptor_pool;
            ds_alloc.descriptorSetCount = 1;
            ds_alloc.pSetLayouts = &descriptor_set_layout_;

            VkDescriptorSet dset = VK_NULL_HANDLE;
            if (fn.vkAllocateDescriptorSets(dev, &ds_alloc, &dset) == VK_SUCCESS) {
                VkDescriptorBufferInfo buf_info{};
                buf_info.buffer = fb.ubo_buffer;
                buf_info.offset = 0;
                buf_info.range = kUiUboSize;

                std::array<VkDescriptorImageInfo, ui::kUiTextureSlotCount> img_infos{};
                std::array<VkWriteDescriptorSet, 1 + ui::kUiTextureSlotCount> writes{};

                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = dset;
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[0].pBufferInfo = &buf_info;

                for (std::size_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
                    VkImageView view = fallback_texture_.view;
                    const std::uint32_t page = bound_pages_[slot];
                    const auto& slot_tex = textures_[slot];
                    if (page < slot_tex.size() && slot_tex[page].view != VK_NULL_HANDLE) {
                        view = slot_tex[page].view;
                    }
                    img_infos[slot].sampler = sampler_;
                    img_infos[slot].imageView = view;
                    img_infos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    writes[1 + slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[1 + slot].dstSet = dset;
                    writes[1 + slot].dstBinding = static_cast<std::uint32_t>(1 + slot);
                    writes[1 + slot].descriptorCount = 1;
                    writes[1 + slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[1 + slot].pImageInfo = &img_infos[slot];
                }

                fn.vkUpdateDescriptorSets(
                    dev, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
                fn.vkCmdBindDescriptorSets(
                    active_cmd_,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_layout_,
                    0,
                    1,
                    &dset,
                    0,
                    nullptr);
                fn.vkCmdDrawIndexed(
                    active_cmd_, batch.index_count, 1, batch.index_offset, 0, 0);
            }
        }
        ++draw_calls_;
    }
}

void VulkanUiDevice::end_frame() {
    bound_pages_.fill(ui::UiBatch::kUnbound);
    active_cmd_ = VK_NULL_HANDLE;
    active_render_pass_ = VK_NULL_HANDLE;
}

std::size_t VulkanUiDevice::texture_page_count(ui::UiTextureSlot slot) const noexcept {
    return textures_[static_cast<std::size_t>(slot)].size();
}

} // namespace engine::render
