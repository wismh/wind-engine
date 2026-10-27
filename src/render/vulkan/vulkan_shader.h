#pragma once

// docs/tech/modules/Render.md

#include "vulkan_context.h"
#include "vulkan_spirv.h"

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::render {

class VulkanShader final : public IShader {
public:
    VulkanShader(std::shared_ptr<VulkanContext> context, const ShaderDesc& desc);
    ~VulkanShader() override;

    VulkanShader(const VulkanShader&) = delete;
    VulkanShader& operator=(const VulkanShader&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool ensure_modules();

    [[nodiscard]] VkShaderModule vertex_module() const noexcept { return vertex_module_; }
    [[nodiscard]] VkShaderModule fragment_module() const noexcept { return fragment_module_; }
    [[nodiscard]] const SpirvReflection& reflection() const noexcept { return reflection_; }

private:
    void destroy_modules();

    std::shared_ptr<VulkanContext> context_;
    std::vector<std::uint32_t> vertex_spirv_;
    std::vector<std::uint32_t> fragment_spirv_;
    SpirvReflection reflection_{};
    VkShaderModule vertex_module_ = VK_NULL_HANDLE;
    VkShaderModule fragment_module_ = VK_NULL_HANDLE;
    bool valid_ = false;
};

} // namespace engine::render
