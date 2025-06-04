#include "engine_host_play.h"

#include <engine/core/engine_host.h>
#include <engine/resources/meta.h>

namespace editor {
namespace {

std::string_view meta_error_text(engine::MetaError error) {
    switch (error) {
        case engine::MetaError::InvalidToml:
            return "invalid TOML";
        case engine::MetaError::MissingGuid:
            return "missing guid";
        case engine::MetaError::MissingImporter:
            return "missing importer";
        case engine::MetaError::InvalidGuid:
            return "invalid guid";
        case engine::MetaError::UnknownImporter:
            return "unknown importer";
        case engine::MetaError::InvalidField:
            return "invalid field";
        case engine::MetaError::Io:
            return "cannot read";
    }
    return "error";
}

}

EngineHostPlay::EngineHostPlay(engine::EngineHost& host) : host_(&host) {}

std::expected<void, std::string> EngineHostPlay::load_catalog(const std::filesystem::path& assets_dir) {
    const auto loaded = host_->load_catalog(assets_dir);
    if (!loaded) {
        return std::unexpected("Game catalog: " + std::string(meta_error_text(loaded.error())));
    }
    return {};
}

void EngineHostPlay::unload_catalog(const std::filesystem::path& assets_dir) {
    host_->unload_catalog(assets_dir);
}

void EngineHostPlay::attach(engine::IGame& game) {
    host_->attach_game(game);
}

void EngineHostPlay::detach() {
    host_->detach_game();
}

}
