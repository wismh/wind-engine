#include "thumbnail_cache.h"

#include <engine/resources/assets_db.h>
#include <engine/resources/image_io.h>
#include <engine/resources/meta.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace editor {
namespace {

std::optional<std::string> read_bytes(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > limit) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_atomic(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(temp, file, error);
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

// Cache files of `guid` other than `keep`: the thumbnails of an older stamp.
void remove_stale(const std::filesystem::path& directory, std::string_view guid, const std::filesystem::path& keep) {
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    const std::string prefix = std::string(guid) + ".";
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const std::filesystem::path& path = it->path();
        if (path == keep || !path.filename().string().starts_with(prefix)) {
            continue;
        }
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

// `<project>/.wind/.gitignore` with `*`, so the cache stays out of version control. Left alone once it exists.
void ignore_cache_folder(const std::filesystem::path& project) {
    const std::filesystem::path file = project / ".wind" / ".gitignore";
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        return;
    }
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    out << "*\n";
}

// `image` centered on a transparent square of its longer side. The UI stretches an image to its box, so a square
// thumbnail keeps its aspect in a square icon slot.
engine::render::TextureDesc pad_square(const engine::render::TextureDesc& image) {
    if (image.width == image.height) {
        return image;
    }
    const int side = std::max(image.width, image.height);
    engine::render::TextureDesc out;
    out.width = side;
    out.height = side;
    out.filter = image.filter;
    out.wrap = image.wrap;
    out.rgba.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u, 0u);
    const int left = (side - image.width) / 2;
    const int top = (side - image.height) / 2;
    const std::size_t row_bytes = static_cast<std::size_t>(image.width) * 4u;
    for (int y = 0; y < image.height; ++y) {
        const std::size_t from = static_cast<std::size_t>(y) * row_bytes;
        const std::size_t to = (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(side) +
                                       static_cast<std::size_t>(left)) * 4u;
        std::copy_n(image.rgba.begin() + static_cast<std::ptrdiff_t>(from), row_bytes,
                out.rgba.begin() + static_cast<std::ptrdiff_t>(to));
    }
    return out;
}

std::uint64_t fnv1a(std::string_view text, std::uint64_t hash) {
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

// 32 lowercase hex digits of two FNV-1a passes over `text`.
engine::AssetId id_from_text(const std::string& text) {
    const std::uint64_t high = fnv1a(text, 0xcbf29ce484222325ull);
    const std::uint64_t low = fnv1a(text, 0x84222325cbf29ce4ull ^ high);
    return engine::AssetId{std::format("{:016x}{:016x}", high, low)};
}

}

std::filesystem::path thumbnail_cache_file(const std::filesystem::path& project, const engine::AssetId& guid,
        const ThumbnailStamp& stamp) {
    const std::string_view hex = guid.hex();
    return project / ".wind" / "cache" / "thumbnails" / std::format("v{}", kThumbnailCacheVersion) /
            std::string(hex.substr(0, 2)) / std::format("{}.{}.{}.png", hex, stamp.size, stamp.modified);
}

engine::AssetId thumbnail_ui_id(const engine::AssetId& guid, const ThumbnailStamp& stamp) {
    return id_from_text(std::format("{}:{}:{}", guid.hex(), stamp.size, stamp.modified));
}

engine::AssetId preview_ui_id(const std::string& key, const ThumbnailStamp& stamp) {
    return id_from_text(std::format("preview:{}:{}:{}", key, stamp.size, stamp.modified));
}

std::optional<engine::render::TextureDesc> build_preview(const std::filesystem::path& png) {
    const std::optional<std::string> bytes = read_bytes(png, kMaxThumbnailSourceBytes);
    if (!bytes) {
        return std::nullopt;
    }
    const std::optional<engine::render::TextureDesc> full = engine::decode_png_rgba(*bytes);
    if (!full) {
        return std::nullopt;
    }
    engine::render::TextureDesc fitted = engine::fit_within(*full, kPreviewEdge);
    if (fitted.width <= 0) {
        return std::nullopt;
    }
    return fitted;
}

std::optional<Thumbnail> build_thumbnail(const std::filesystem::path& project, const std::filesystem::path& png,
        const ThumbnailStamp& stamp) {
    std::filesystem::path meta_path = png;
    meta_path += ".meta";
    const std::optional<std::string> meta_text = read_bytes(meta_path, 1024 * 1024);
    if (!meta_text) {
        return std::nullopt;
    }
    const auto meta = engine::parse_asset_meta(*meta_text);
    if (!meta) {
        return std::nullopt;
    }
    const engine::AssetId guid = meta->guid;
    const std::filesystem::path cached = thumbnail_cache_file(project, guid, stamp);

    if (const std::optional<std::string> bytes = read_bytes(cached, kMaxThumbnailSourceBytes)) {
        if (auto image = engine::decode_png_rgba(*bytes)) {
            return Thumbnail{guid, std::move(*image)};
        }
    }

    const std::optional<std::string> bytes = read_bytes(png, kMaxThumbnailSourceBytes);
    if (!bytes) {
        return std::nullopt;
    }
    const std::optional<engine::render::TextureDesc> full = engine::decode_png_rgba(*bytes);
    if (!full) {
        return std::nullopt;
    }
    const engine::render::TextureDesc fitted = engine::fit_within(*full, kThumbnailEdge);
    if (fitted.width <= 0) {
        return std::nullopt;
    }
    engine::render::TextureDesc small = pad_square(fitted);
    ignore_cache_folder(project);
    if (write_atomic(cached, engine::encode_png_rgba(small))) {
        remove_stale(cached.parent_path(), guid.hex(), cached);
    }
    return Thumbnail{guid, std::move(small)};
}

ThumbnailService::ThumbnailService() = default;

ThumbnailService::~ThumbnailService() {
    stop_worker();
}

void ThumbnailService::stop_worker() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
}

void ThumbnailService::reset() {
    jobs_.clear();
    done_.clear();
    busy_ = false;
    entries_.clear();
    ready_ = 0;
    previews_.clear();
    previews_ready_ = 0;
}

void ThumbnailService::open(const std::filesystem::path& project) {
    stop_worker();
    reset();
    project_ = project;
    worker_ = std::jthread([this, project](std::stop_token stop) { work(stop, project); });
}

void ThumbnailService::close() {
    stop_worker();
    reset();
    project_.clear();
}

void ThumbnailService::request(bool preview, const std::string& key, const std::filesystem::path& png,
        const ThumbnailStamp& stamp) {
    std::unordered_map<std::string, Entry>& entries = preview ? previews_ : entries_;
    std::size_t& ready = preview ? previews_ready_ : ready_;
    const auto found = entries.find(key);
    if (found != entries.end() && found->second.state == State::Ready && ready > 0) {
        --ready;
    }
    entries.insert_or_assign(key, Entry{.stamp = stamp});
    {
        const std::scoped_lock lock(mutex_);
        jobs_.push_back(Job{preview, key, png, stamp});
    }
    wake_.notify_one();
}

std::optional<engine::AssetId> ThumbnailService::find_or_request(const std::string& key,
        const std::filesystem::path& png, const ThumbnailStamp& stamp) {
    if (project_.empty()) {
        return std::nullopt;
    }
    const auto found = entries_.find(key);
    if (found != entries_.end() && found->second.stamp == stamp) {
        return found->second.state == State::Ready ? std::optional(found->second.ui_id) : std::nullopt;
    }
    if (ready_ < kMaxThumbnails) {
        request(false, key, png, stamp);
    }
    return std::nullopt;
}

std::optional<PreviewImage> ThumbnailService::find_or_request_preview(const std::string& key,
        const std::filesystem::path& png, const ThumbnailStamp& stamp) {
    if (project_.empty()) {
        return std::nullopt;
    }
    const auto found = previews_.find(key);
    if (found != previews_.end() && found->second.stamp == stamp) {
        const Entry& entry = found->second;
        return entry.state == State::Ready ? std::optional(PreviewImage{entry.ui_id, entry.width, entry.height})
                                           : std::nullopt;
    }
    if (previews_ready_ < kMaxPreviews) {
        request(true, key, png, stamp);
    }
    return std::nullopt;
}

bool ThumbnailService::poll(engine::AssetsDb& assets) {
    std::vector<Done> finished;
    {
        const std::scoped_lock lock(mutex_);
        finished.swap(done_);
    }
    bool news = false;
    for (Done& item : finished) {
        std::unordered_map<std::string, Entry>& entries = item.preview ? previews_ : entries_;
        const auto it = entries.find(item.key);
        if (it == entries.end() || !(it->second.stamp == item.stamp)) {
            continue;
        }
        if (!item.thumbnail) {
            it->second.state = State::Failed;
            continue;
        }
        const engine::AssetId id = item.preview ? preview_ui_id(item.key, item.stamp)
                                                : thumbnail_ui_id(item.thumbnail->guid, item.stamp);
        it->second.width = item.thumbnail->image.width;
        it->second.height = item.thumbnail->image.height;
        assets.set_runtime_texture(id, std::move(item.thumbnail->image));
        it->second.ui_id = id;
        it->second.state = State::Ready;
        ++(item.preview ? previews_ready_ : ready_);
        news = true;
    }
    return news;
}

std::size_t ThumbnailService::pending_count() const {
    const std::scoped_lock lock(mutex_);
    return jobs_.size() + (busy_ ? 1u : 0u);
}

void ThumbnailService::work(std::stop_token stop, std::filesystem::path project) {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [this] { return !jobs_.empty(); });
            if (stop.stop_requested()) {
                return;
            }
            job = std::move(jobs_.back());
            jobs_.pop_back();
            busy_ = true;
        }
        std::optional<Thumbnail> made;
        if (job.preview) {
            if (std::optional<engine::render::TextureDesc> image = build_preview(job.png)) {
                made = Thumbnail{engine::AssetId{}, std::move(*image)};
            }
        } else {
            made = build_thumbnail(project, job.png, job.stamp);
        }
        {
            const std::scoped_lock lock(mutex_);
            done_.push_back(Done{job.preview, std::move(job.key), job.stamp, std::move(made)});
            busy_ = false;
        }
    }
}

}
