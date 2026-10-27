#include <engine/builtin_ids.h>
#include <engine/project/wind_project.h>
#include <engine/resources/component_codegen.h>
#include <engine/resources/meta.h>

#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc >= 3 && std::string_view(argv[1]) == "--components") {
        const std::filesystem::path output_file = argv[2];
        std::vector<std::filesystem::path> dirs;
        for (int i = 3; i < argc; ++i) {
            dirs.emplace_back(argv[i]);
        }
        const auto result = engine::component_codegen_write(dirs, output_file);
        if (!result) {
            std::cerr << result.error().message << '\n';
            return 1;
        }
        return 0;
    }

    if (argc < 3) {
        std::cerr << "usage: asset_codegen <assets_dir> <output_dir> [--engine] [--project <dir>] [--stage <dir>]\n"
                  << "       asset_codegen --components <output_file> [<source_dir>...]\n";
        return 2;
    }

    const std::filesystem::path assets_dir = argv[1];
    const std::filesystem::path output_dir = argv[2];
    bool engine_builtins = false;
    std::filesystem::path project_dir;
    std::filesystem::path stage_dir;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--engine") {
            engine_builtins = true;
        } else if (arg == "--project" && i + 1 < argc) {
            project_dir = argv[++i];
        } else if (arg == "--stage" && i + 1 < argc) {
            stage_dir = argv[++i];
        }
    }

    std::vector<std::string> roots;
    std::vector<std::string> always_include;

    if (!engine_builtins) {
        if (project_dir.empty()) {
            const auto candidate = assets_dir.parent_path();
            if (std::filesystem::is_regular_file(candidate / engine::kWindProjectFile)) {
                project_dir = candidate;
            }
        }
        if (!project_dir.empty() && std::filesystem::is_regular_file(project_dir / engine::kWindProjectFile)) {
            auto proj = engine::read_wind_project(project_dir);
            if (!proj) {
                std::cerr << "failed to read " << engine::kWindProjectFile << ": " << proj.error().detail << '\n';
                return 1;
            }
            roots = std::move(proj->asset_roots);
            always_include = std::move(proj->asset_always_include);
        }
    }

    const std::span<const engine::AssetId> reserved =
            engine_builtins ? std::span<const engine::AssetId>{} : engine::builtin::reserved();
    const auto result = engine::codegen_write(assets_dir, output_dir, reserved, roots, always_include, stage_dir);
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    return 0;
}
