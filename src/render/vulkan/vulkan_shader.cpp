#include "vulkan_shader.h"

#include <utility>

namespace engine::render {

VulkanShader::VulkanShader(std::shared_ptr<VulkanContext> context, const ShaderDesc& desc)
    : context_(std::move(context))
    , vertex_spirv_(desc.vertex_spirv)
    , fragment_spirv_(desc.fragment_spirv) {
    const bool has_spirv = !vertex_spirv_.empty() && !fragment_spirv_.empty();
    const bool has_glsl = !desc.vertex_src.empty() && !desc.fragment_src.empty();
    if (!has_spirv && !has_glsl) {
        return;
    }
    valid_ = true;
    if (has_spirv) {
        reflection_ = reflect_spirv_program(vertex_spirv_, fragment_spirv_);
        if (context_ && context_->is_device_ready()) {
            if (!ensure_modules()) {
                valid_ = false;
            }
        }
    }
}

VulkanShader::~VulkanShader() {
    destroy_modules();
}

bool VulkanShader::ensure_modules() {
    if (!valid_ || vertex_spirv_.empty() || fragment_spirv_.empty()) {
        return false;
    }
    if (vertex_module_ != VK_NULL_HANDLE && fragment_module_ != VK_NULL_HANDLE) {
        return true;
    }
    if (!context_ || !context_->is_device_ready()) {
        return false;
    }

    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (!fn.vkCreateShaderModule || device == VK_NULL_HANDLE) {
        return false;
    }

    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = vertex_spirv_.size() * sizeof(std::uint32_t);
    vert_info.pCode = vertex_spirv_.data();
    if (fn.vkCreateShaderModule(device, &vert_info, nullptr, &vertex_module_) != VK_SUCCESS) {
        vertex_module_ = VK_NULL_HANDLE;
        return false;
    }

    VkShaderModuleCreateInfo frag_info{};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = fragment_spirv_.size() * sizeof(std::uint32_t);
    frag_info.pCode = fragment_spirv_.data();
    if (fn.vkCreateShaderModule(device, &frag_info, nullptr, &fragment_module_) != VK_SUCCESS) {
        if (fn.vkDestroyShaderModule) {
            fn.vkDestroyShaderModule(device, vertex_module_, nullptr);
        }
        vertex_module_ = VK_NULL_HANDLE;
        fragment_module_ = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void VulkanShader::destroy_modules() {
    if (!context_ || !context_->is_device_ready()) {
        vertex_module_ = VK_NULL_HANDLE;
        fragment_module_ = VK_NULL_HANDLE;
        return;
    }
    const VulkanFunctions& fn = context_->fn();
    const VkDevice device = context_->device();
    if (!fn.vkDestroyShaderModule || device == VK_NULL_HANDLE) {
        return;
    }
    if (vertex_module_ != VK_NULL_HANDLE) {
        fn.vkDestroyShaderModule(device, vertex_module_, nullptr);
        vertex_module_ = VK_NULL_HANDLE;
    }
    if (fragment_module_ != VK_NULL_HANDLE) {
        fn.vkDestroyShaderModule(device, fragment_module_, nullptr);
        fragment_module_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::render
