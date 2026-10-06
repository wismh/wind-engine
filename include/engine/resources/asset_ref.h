#pragma once

#include <engine/resources/asset_filter.h>
#include <engine/resources/asset_id.h>

#include <expected>
#include <memory>
#include <string_view>

namespace engine {

class AssetsDb;
enum class AssetError;

template<typename T>
class AssetRef {
public:
    constexpr AssetRef() noexcept = default;
    constexpr explicit AssetRef(AssetId asset_id) noexcept : id_(asset_id) {}
    constexpr explicit AssetRef(std::string_view hex) : id_(hex) {}

    [[nodiscard]] constexpr AssetId id() const noexcept { return id_; }
    [[nodiscard]] constexpr AssetId guid() const noexcept { return id_; }
    [[nodiscard]] constexpr explicit operator AssetId() const noexcept { return id_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return id_ != AssetId{}; }

    constexpr bool operator==(const AssetRef&) const noexcept = default;
    constexpr auto operator<=>(const AssetRef&) const noexcept = default;

    [[nodiscard]] static AssetFilter filter() { return asset_filter_for<T>(); }

    [[nodiscard]] std::shared_ptr<T> get(AssetsDb& db) const;
    [[nodiscard]] std::expected<std::shared_ptr<T>, AssetError> try_get(AssetsDb& db) const;

private:
    AssetId id_{};
};

} // namespace engine
