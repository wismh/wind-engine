#include "render/context_objects.h"

#include <algorithm>
#include <utility>

namespace engine::render {

ContextObjects::Key ContextObjects::add_context(const void* native) {
    if (native == nullptr) {
        return 0;
    }
    remove_context(native);
    const Key key = next_key_++;
    contexts_.push_back(Context{.native = native, .key = key});
    return key;
}

void ContextObjects::remove_context(const void* native) {
    std::erase_if(contexts_, [native](const Context& context) { return context.native == native; });
}

ContextObjects::Key ContextObjects::key(const void* native) const noexcept {
    if (native == nullptr) {
        return 0;
    }
    const auto it = std::ranges::find(contexts_, native, &Context::native);
    return it != contexts_.end() ? it->key : 0;
}

bool ContextObjects::is_live(Key key) const noexcept {
    return key != 0 && std::ranges::find(contexts_, key, &Context::key) != contexts_.end();
}

void ContextObjects::defer_delete(Key key, unsigned int vao) {
    if (vao == 0) {
        return;
    }
    const auto it = std::ranges::find(contexts_, key, &Context::key);
    if (key != 0 && it != contexts_.end()) {
        it->deferred.push_back(vao);
    }
}

std::vector<unsigned int> ContextObjects::take_deferred(Key key) {
    const auto it = std::ranges::find(contexts_, key, &Context::key);
    if (key == 0 || it == contexts_.end()) {
        return {};
    }
    return std::exchange(it->deferred, {});
}

}
