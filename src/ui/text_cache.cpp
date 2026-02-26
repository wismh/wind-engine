#include "ui/text_cache.h"

#include <cstring>

namespace engine::ui {

    namespace {

        // A document's text cache stops growing here: a list of ever-new strings must not keep every one alive.
        constexpr std::size_t kMaxEntries = 8192;

        thread_local SharedTextCache *g_active = nullptr;

    } // namespace

    TextCacheScope::TextCacheScope(const UiDocument &document, const IUiPainter *painter) : previous_(g_active) {
        SharedTextCache &cache = document.text_cache;
        if (cache.painter != static_cast<const void *>(painter) ||
            cache.measures.size() + cache.blocks.size() > kMaxEntries) {
            cache.measures.clear();
            cache.blocks.clear();
            cache.painter = painter;
        }
        g_active = &cache;
    }

    TextCacheScope::~TextCacheScope() { g_active = previous_; }

    SharedTextCache *active_text_cache() noexcept { return g_active; }

    std::string text_cache_key(std::string_view text, const AssetId &font, float size, float width) {
        std::string key;
        key.reserve(text.size() + 48);
        key.append(text);
        key.push_back('\x1f');
        key.append(font.hex());
        key.push_back('\x1f');
        char bits[sizeof(float) * 2];
        std::memcpy(bits, &size, sizeof(float));
        std::memcpy(bits + sizeof(float), &width, sizeof(float));
        key.append(bits, sizeof(bits));
        return key;
    }

} // namespace engine::ui
