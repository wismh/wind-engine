#pragma once

#include <engine/resources/asset_id.h>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class MetaError {
    InvalidToml,
    MissingGuid,
    MissingImporter,
    InvalidGuid,
    UnknownImporter,
    InvalidField,
};

enum class ImporterKind {
    Texture,
    Audio,
    Mesh,
    Shader,
    Font,
    UiImage,
    Material,
    Ui,
    Css,
};

enum class ColorSpace {
    Srgb,
    Linear,
};

enum class FilterMode {
    Nearest,
    Linear,
};

enum class WrapMode {
    Clamp,
    Repeat,
    Mirror,
};

enum class TextureLayout {
    Single,
    Multiple,
};

enum class AudioBank {
    Sfx,
    Music,
};

struct TextureImportSettings {
    ColorSpace color_space = ColorSpace::Srgb;
    FilterMode filter = FilterMode::Linear;
    WrapMode wrap = WrapMode::Clamp;
    TextureLayout layout = TextureLayout::Single;
};

struct AudioImportSettings {
    AudioBank bank = AudioBank::Sfx;
    float volume = 1.0f;
    float pitch_min = 1.0f;
    float pitch_max = 1.0f;
    bool loop = false;
};

struct AssetMeta {
    AssetId guid;
    ImporterKind importer = ImporterKind::Texture;
    TextureImportSettings texture;
    AudioImportSettings audio;
};

struct CatalogEntry {
    AssetId guid;
    std::string relative_path;
    ImporterKind importer = ImporterKind::Texture;
};

class CookedCatalog {
public:
    void add(CatalogEntry entry);
    [[nodiscard]] const CatalogEntry* find(AssetId id) const;
    [[nodiscard]] const std::vector<CatalogEntry>& entries() const noexcept;

    [[nodiscard]] std::string serialize() const;

private:
    std::vector<CatalogEntry> entries_;
};

struct AssetCppId {
    std::vector<std::string> namespaces;
    std::string name;

    [[nodiscard]] std::string qualified() const;
};

enum class CodegenErrorKind {
    MissingMeta,
    InvalidGuid,
    Collision,
    InvalidMeta,
    Io,
};

struct CodegenError {
    CodegenErrorKind kind = CodegenErrorKind::Io;
    std::string message;
};

struct CodegenOutput {
    CookedCatalog catalog;
    std::string asset_ids_header;
};

[[nodiscard]] std::expected<AssetMeta, MetaError> parse_asset_meta(std::string_view toml_text);
[[nodiscard]] std::expected<CookedCatalog, MetaError> parse_cooked_catalog(std::string_view toml_text);

[[nodiscard]] AssetCppId identifier_from_path(std::string_view relative_path);

[[nodiscard]] std::expected<CodegenOutput, CodegenError> codegen_scan(const std::filesystem::path& assets_root);
[[nodiscard]] std::expected<void, CodegenError> codegen_write(const std::filesystem::path& assets_root,
        const std::filesystem::path& output_dir);

[[nodiscard]] std::string_view to_string(ImporterKind kind) noexcept;

}
