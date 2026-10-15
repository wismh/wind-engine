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

} // namespace engine::ui
