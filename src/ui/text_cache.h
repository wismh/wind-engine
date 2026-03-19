#pragma once

// docs/tech/modules/UI.md (Text measure cache)

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/text_line.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::ui {

    // While alive, measuring on this thread reads and fills `document.text_cache`. The cache is emptied when `painter`
    // is not the one it was filled for.
    class TextCacheScope {
    public:
        TextCacheScope(const UiDocument &document, const IUiPainter *painter);
        ~TextCacheScope();
        TextCacheScope(const TextCacheScope &) = delete;
        TextCacheScope &operator=(const TextCacheScope &) = delete;

    private:
        SharedTextCache *previous_;
    };

    // The cache of the innermost live scope, or null outside one.
    [[nodiscard]] SharedTextCache *active_text_cache() noexcept;


} // namespace engine::ui
