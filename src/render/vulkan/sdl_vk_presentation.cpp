#include "sdl_vk_presentation.h"

#include "vulkan_backend.h"
#include "vulkan_canvas.h"
#include "vulkan_context.h"
#include "vulkan_factory.h"

#include "render/opengl/sdl_gl_presentation.h"

#include <memory>

namespace engine {

std::unique_ptr<IPresentation> make_sdl_vk_presentation() {
    auto context = std::make_shared<render::VulkanContext>();
    auto factory = std::make_shared<render::VulkanFactory>(context);
    auto backend = std::make_shared<render::VulkanRenderBackend>(context);
    return make_sdl_presentation(
        factory,
        backend,
        [backend](
            WindowSystem& window,
            render::CommandBuffer& commands) -> std::shared_ptr<render::IWindowCanvas> {
            return std::make_shared<render::VulkanCanvas>(window, commands, *backend);
        });
}

} // namespace engine
