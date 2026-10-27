#include <engine/project/wind_project.h>

#include "project/manifest_table.h"

#include <utility>

namespace engine {

std::expected<WindProject, ManifestFailure> read_wind_project(const std::filesystem::path& directory) {
    const std::filesystem::path path = directory / kWindProjectFile;
    const std::expected<ManifestTable, ManifestFailure> table = ManifestTable::load(path);
    if (!table) {
        return std::unexpected(table.error());
    }
    std::expected<std::optional<std::string>, ManifestFailure> name = table->optional_string("name");
    if (!name) {
        return std::unexpected(in_manifest(std::move(name.error()), path));
    }
    std::expected<std::string, ManifestFailure> engine = table->string("engine");
    if (!engine) {
        return std::unexpected(in_manifest(std::move(engine.error()), path));
    }
    std::expected<std::string, ManifestFailure> target = table->string("target");
    if (!target) {
        return std::unexpected(in_manifest(std::move(target.error()), path));
    }
    auto roots = table->optional_string_array("assets", "roots");
    if (!roots) {
        return std::unexpected(in_manifest(std::move(roots.error()), path));
    }
    auto always_include = table->optional_string_array("assets", "always_include");
    if (!always_include) {
        return std::unexpected(in_manifest(std::move(always_include.error()), path));
    }
    // A directory given with a trailing separator has an empty filename.
    const std::filesystem::path folder = directory.has_filename() ? directory : directory.parent_path();
    return WindProject{
            .name = name->value_or(manifest_path_text(folder.filename())),
            .engine = std::move(*engine),
            .target = std::move(*target),
            .asset_roots = std::move(*roots),
            .asset_always_include = std::move(*always_include),
    };
}

}
