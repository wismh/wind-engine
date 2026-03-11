#pragma once

#include <cstdint>
#include <vector>

namespace engine::render {

// Live GL contexts and the objects one context cannot share with another. Textures, buffers, and programs are shared
// between every window's context; a vertex array object is not. A VAO name made in one context means nothing, or
// another VAO, in the next. So a VAO is made once per context, and one owned by a context that is not current is
// deleted the next time that context is flushed.
//
// A context is known by its native handle, which the driver may reuse after it is destroyed. The key a context gets
// is never reused, so a VAO cached for a destroyed context never matches a new one with the same handle.
class ContextObjects {
public:
    // 0 is no context.
    using Key = std::uint64_t;

    Key add_context(const void* native);
    // Forgets the context and drops its pending deletes: destroying a context frees its VAOs.
    void remove_context(const void* native);
    [[nodiscard]] Key key(const void* native) const noexcept;
    [[nodiscard]] bool is_live(Key key) const noexcept;
    // A VAO of `key` that cannot be deleted now because another context is current. Dropped if `key` is not live.
    void defer_delete(Key key, unsigned int vao);
    // The VAOs deferred for `key`, now that it is current. The list for `key` is left empty.
    [[nodiscard]] std::vector<unsigned int> take_deferred(Key key);

private:
    struct Context {
        const void* native = nullptr;
        Key key = 0;
        std::vector<unsigned int> deferred;
    };

    std::vector<Context> contexts_;
    Key next_key_ = 1;
};

}
