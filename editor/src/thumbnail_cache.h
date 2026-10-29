#pragma once

#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine {
class AssetsDb;
}

namespace editor {

// Longest side of a thumbnail, in pixels. Changing it (or how one is made) moves `kThumbnailCacheVersion`.
inline constexpr int kThumbnailEdge = 128;
inline constexpr int kThumbnailCacheVersion = 1;
// A session keeps at most this many thumbnails as UI images (the window's image atlas never frees one). Past it
// a texture keeps its type icon.
inline constexpr std::size_t kMaxThumbnails = 1024;
// The Inspector's preview of a selected texture: its longer side, and how many a session keeps (the same atlas
// limit as above).
inline constexpr int kPreviewEdge = 256;
inline constexpr std::size_t kMaxPreviews = 128;
// A PNG larger than this on disk is not decoded for a thumbnail.
inline constexpr std::uintmax_t kMaxThumbnailSourceBytes = 64u * 1024u * 1024u;

// What a file looked like when its thumbnail was made. A change to either makes a new thumbnail.
struct ThumbnailStamp {
    std::uintmax_t size = 0;
    std::int64_t modified = 0;

    bool operator==(const ThumbnailStamp&) const = default;
};

// `<project>/.wind/cache/thumbnails/v<N>/<first two hex of guid>/<guid>.<size>.<modified>.png`. The stamp is in the
// name, so a file that changed is a miss and the old one is replaced when the new one is written.
[[nodiscard]] std::filesystem::path thumbnail_cache_file(const std::filesystem::path& project,
        const engine::AssetId& guid, const ThumbnailStamp& stamp);

// The id the UI draws a thumbnail by. Derived from the GUID and the stamp, so a changed file is a new image
// (the window's atlas keeps the pixels it already registered for an id).
[[nodiscard]] engine::AssetId thumbnail_ui_id(const engine::AssetId& guid, const ThumbnailStamp& stamp);

// The id the UI draws the preview of `key` by, as `thumbnail_ui_id` does for a thumbnail.
[[nodiscard]] engine::AssetId preview_ui_id(const std::string& key, const ThumbnailStamp& stamp);

struct Thumbnail {
    engine::AssetId guid;
    engine::render::TextureDesc image;
};

// The thumbnail of the PNG at `png` (its `.meta` sidecar names the GUID): from the cache when the stamp matches,
// otherwise decoded, scaled to `kThumbnailEdge`, and written to the cache (a failed write still returns it).
// Nullopt without a readable sidecar, for a file over `kMaxThumbnailSourceBytes`, or for bytes that are not a PNG.
// Reads and writes files only: a worker thread may call it.
[[nodiscard]] std::optional<Thumbnail> build_thumbnail(const std::filesystem::path& project,
        const std::filesystem::path& png, const ThumbnailStamp& stamp);

// The PNG at `png` scaled to `kPreviewEdge`, aspect kept. Nullopt for a file over `kMaxThumbnailSourceBytes` or one
// that is not a PNG. Reads files only: a worker thread may call it.
[[nodiscard]] std::optional<engine::render::TextureDesc> build_preview(const std::filesystem::path& png);

// A preview that is ready: the UI image, and its size in pixels.
struct PreviewImage {
    engine::AssetId id;
    int width = 0;
    int height = 0;
};

// Makes thumbnails of project PNGs on one worker thread and hands them to the UI. Every member but the worker
// runs on the editor thread. A request is made again only when its stamp changes.
class ThumbnailService {
public:
    ThumbnailService();
    ~ThumbnailService();

    ThumbnailService(const ThumbnailService&) = delete;
    ThumbnailService& operator=(const ThumbnailService&) = delete;

    // Forgets every thumbnail and request, and works for `project` from now on. Pending work is dropped.
    void open(const std::filesystem::path& project);
    void close();

    // The UI image of `key` once its thumbnail for `stamp` is ready. Otherwise nullopt, and the thumbnail is asked
    // for (once per stamp); the newest request is made first, so a scroll shows what is on screen before the rest.
    [[nodiscard]] std::optional<engine::AssetId> find_or_request(const std::string& key,
            const std::filesystem::path& png, const ThumbnailStamp& stamp);

    // The same for the Inspector: `png` scaled to `kPreviewEdge`, not cached on disk, asked for once per stamp.
    [[nodiscard]] std::optional<PreviewImage> find_or_request_preview(const std::string& key,
            const std::filesystem::path& png, const ThumbnailStamp& stamp);

    // Registers what the worker finished as runtime textures of `assets`. True when a new one is ready, so rows
    // that wait for it should be shown again.
    [[nodiscard]] bool poll(engine::AssetsDb& assets);

    [[nodiscard]] std::size_t ready_count() const { return ready_; }
    // Waiting or running on the worker.
    [[nodiscard]] std::size_t pending_count() const;

private:
    enum class State {
        Pending,
        Ready,
        Failed,
    };
    struct Entry {
        ThumbnailStamp stamp;
        State state = State::Pending;
        engine::AssetId ui_id;
        int width = 0;
        int height = 0;
    };
    struct Job {
        bool preview = false;
        std::string key;
        std::filesystem::path png;
        ThumbnailStamp stamp;
    };
    struct Done {
        bool preview = false;
        std::string key;
        ThumbnailStamp stamp;
        std::optional<Thumbnail> thumbnail;
    };

    void stop_worker();
    void reset();
    void request(bool preview, const std::string& key, const std::filesystem::path& png, const ThumbnailStamp& stamp);
    void work(std::stop_token stop, std::filesystem::path project);

    std::filesystem::path project_;
    std::unordered_map<std::string, Entry> entries_;
    std::size_t ready_ = 0;
    std::unordered_map<std::string, Entry> previews_;
    std::size_t previews_ready_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<Job> jobs_;
    std::vector<Done> done_;
    bool busy_ = false;
    std::jthread worker_;
};

}
