# Background Jobs

Some work takes longer than a frame: generating or meshing a terrain chunk, decoding a level, pathfinding over a big map. `JobSystem::submit` runs such a function on a worker thread and hands its result back to the main thread at the start of a later frame, through a `JobCall<T>` you own.

The `JobSystem` comes with `EngineServices` (`services.jobs`). For work that must finish inside the current frame, use `parallel_for` instead ([Particles](../rendering/Particles.md) is an example).

---

## 1. Submit, then take

```cpp
#include <engine/core/job_system.h>
#include <engine/render/graphic_factory.h>

struct ChunkMesh {
    glm::ivec3 chunk;
    engine::render::MeshDesc mesh;
};

class ChunkLoader {
public:
    explicit ChunkLoader(const engine::EngineServices& services)
        : jobs_(&services.jobs), graphics_(&services.graphics) {}

    void request(glm::ivec3 chunk, std::shared_ptr<const BlockData> blocks, int priority) {
        // The function owns what it reads: the loader may drop the call while the job runs.
        calls_.push_back(jobs_->submit(
                [chunk, blocks] { return ChunkMesh{chunk, build_chunk_mesh(*blocks)}; }, priority));
    }

    // A Frame system calls this. Results appear only at the start of a frame.
    void collect() {
        for (engine::JobCall<ChunkMesh>& call : calls_) {
            if (std::optional<ChunkMesh> done = call.take()) {
                meshes_[done->chunk] = graphics_->create_mesh(done->mesh);  // engine call: main thread only
            }
        }
        std::erase_if(calls_, [](const engine::JobCall<ChunkMesh>& call) { return !call.pending(); });
    }

private:
    engine::JobSystem* jobs_;
    engine::render::IGraphicFactory* graphics_;
    std::vector<engine::JobCall<ChunkMesh>> calls_;
    std::map<glm::ivec3, std::shared_ptr<engine::render::IMesh>, ChunkLess> meshes_;
};
```

- `take()` returns the result once, then the call is empty. It returns `nullopt` while the job is still pending, after a cancel, and on an empty call.
- `pending()` is true from `submit` until the result arrives or the call is cancelled.
- The engine delivers results in `JobSystem::poll`, at the start of every frame, so every system of that frame sees the same state.

---

## 2. Rules for the job function

| Rule | Why |
| --- | --- |
| Return data (`MeshDesc`, a block array, a path) | The main thread turns it into meshes or textures after `take` |
| Never call engine APIs (`World`, `AssetsDb`, `IGraphicFactory`, `submit`) | They are main-thread only |
| Own or share everything it reads (capture by value or `shared_ptr`) | The call's owner may be destroyed while the job runs |
| Do not throw | A throw ends the process (`std::terminate`). Return `std::expected` for failures |
| Return a value | `JobCall<void>` does not exist. Return a `bool` or a small struct |

---

## 3. Cancel and priority

Destroying a `JobCall` cancels it, so a call kept in a component, a view-model, or a game object dies with its owner. `cancel()` does the same explicitly:

- A job that has not started never runs.
- A running job finishes, but its result is thrown away.
- A finished job's result is thrown away.

A long job can stop early. Take a `const engine::JobToken&` and check it:

```cpp
auto call = jobs.submit([blocks](const engine::JobToken& token) {
    ChunkMesh out;
    for (int slice = 0; slice < 16; ++slice) {
        if (token.cancelled()) {
            return out;  // thrown away anyway
        }
        mesh_slice(*blocks, slice, out);
    }
    return out;
});
```

Higher `priority` runs first; equal priorities run in submit order. As the player moves, raise the priority of the chunks that are now nearest: `call.set_priority(new_priority)`. That only moves a job that has not started yet.

---

## 4. Threads and platforms

- Jobs share the worker threads with `parallel_for`. At most `workers - 1` jobs run at once, so `parallel_for` always keeps a free worker.
- With no worker threads (Web without pthreads, a one-core machine), jobs run on the main thread inside `poll`: at least one per frame, then more until about 4 ms have passed. Submit fewer, smaller jobs there.
- `jobs.wait_idle()` blocks until every queued job has run. Use it behind a loading screen. The results still arrive at the next `poll`.

---

## 5. In the editor

On Stop the editor cancels every background job and waits for the running ones before it unloads your game module. A job that ignores its `JobToken` makes Stop wait for as long as it runs. Check the token in long loops.
