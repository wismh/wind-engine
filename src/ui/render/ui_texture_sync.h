#pragma once

#include "ui/render/paint_resources.h"
#include "ui/render/ui_device.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine::ui {

// What one device has of the shared atlas pages, and the uploads that bring it up to date. Every window's device
// keeps its own (its textures are its own), so no device clears a dirty flag another still needs: a page it has
// never seen is created whole, a page whose version moved sends `dirty_since` the version it last uploaded, and a new
// PaintResources::generation drops every texture first. Backend-neutral; a device only receives the calls.
class UiTextureSync {
public:
    // Brings `device` up to date with every page of `resources`. Call inside the device's frame.
    void sync(const PaintResources& resources, IUiDevice& device);

private:
    void forget() noexcept;

    static constexpr std::uint64_t kNoGeneration = ~std::uint64_t{0};

    // Per slot, the atlas version of each page as last uploaded.
    std::array<std::vector<std::uint64_t>, kUiTextureSlotCount> uploaded_;
    std::uint64_t generation_ = kNoGeneration;
};

}
