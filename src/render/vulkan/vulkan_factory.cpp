#include "vulkan_factory.h"

#include "render/mesh_check.h"

#include <engine/log.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine::render {

namespace {

class VulkanMesh final : public IMesh {
public:
    explicit VulkanMesh(const MeshDesc& desc)
        : vertices_(desc.vertices)
        , indices_(desc.indices) {}

    [[nodiscard]] const std::vector<MeshVertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept { return indices_; }

private:
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

class VulkanShader final : public IShader {
public:
    VulkanShader(std::shared_ptr<VulkanContext> context, const ShaderDesc& desc)
        : context_(std::move(context))
        , vertex_spirv_(desc.vertex_spirv)
        , fragment_spirv_(desc.fragment_spirv) {
        if (context_ && context_->is_device_ready() && !vertex_spirv_.empty() && !fragment_spirv_.empty()) {
            create_modules();
        }
    }

    ~VulkanShader() override {
        destroy_modules();
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

private:
    void create_modules() {
        const VulkanFunctions& fn = context_->fn();
        const VkDevice device = context_->device();
        if (!fn.vkCreateShaderModule || device == VK_NULL_HANDLE) {
            return;
        }

        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = vertex_spirv_.size() * sizeof(std::uint32_t);
        vert_info.pCode = vertex_spirv_.data();
        if (fn.vkCreateShaderModule(device, &vert_info, nullptr, &vertex_module_) != VK_SUCCESS) {
            vertex_module_ = VK_NULL_HANDLE;
            valid_ = false;
            return;
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
            valid_ = false;
        }
    }

    void destroy_modules() {
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

    std::shared_ptr<VulkanContext> context_;
    std::vector<std::uint32_t> vertex_spirv_;
    std::vector<std::uint32_t> fragment_spirv_;
    VkShaderModule vertex_module_ = VK_NULL_HANDLE;
    VkShaderModule fragment_module_ = VK_NULL_HANDLE;
    bool valid_ = true;
};

class VulkanTexture final : public IDynamicTexture {
public:
    VulkanTexture(int width, int height, std::vector<std::uint8_t> rgba)
        : width_(width)
        , height_(height)
        , rgba_(std::move(rgba)) {}

    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int height() const noexcept override { return height_; }

    bool update(std::span<const std::uint8_t> rgba, int width, int height) override {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const std::size_t required = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        if (rgba.size() < required) {
            return false;
        }
        width_ = width;
        height_ = height;
        rgba_.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(required));
        return true;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> rgba_;
};

} // namespace

VulkanFactory::VulkanFactory(std::shared_ptr<VulkanContext> context)
    : context_(std::move(context)) {}

std::shared_ptr<IMesh> VulkanFactory::create_mesh(const MeshDesc& desc) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error(error);
        return {};
    }
    return std::make_shared<VulkanMesh>(desc);
}

std::shared_ptr<IShader> VulkanFactory::create_shader(const ShaderDesc& desc) {
    const bool has_spirv = !desc.vertex_spirv.empty() && !desc.fragment_spirv.empty();
    const bool has_glsl = !desc.vertex_src.empty() && !desc.fragment_src.empty();
    if (!has_spirv && !has_glsl) {
        return {};
    }
    auto shader = std::make_shared<VulkanShader>(context_, desc);
    if (!shader->valid()) {
        return {};
    }
    return shader;
}

std::shared_ptr<ITexture> VulkanFactory::create_texture(const TextureDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0) {
        return {};
    }
    const std::size_t required =
        static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4u;
    if (desc.rgba.size() < required) {
        return {};
    }
    return std::make_shared<VulkanTexture>(desc.width, desc.height, desc.rgba);
}

std::shared_ptr<IDynamicTexture> VulkanFactory::create_dynamic_texture(const DynamicTextureDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0) {
        return {};
    }
    const std::size_t required =
        static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4u;
    return std::make_shared<VulkanTexture>(
        desc.width, desc.height, std::vector<std::uint8_t>(required, 0));
}

} // namespace engine::render
