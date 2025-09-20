#include "batch_run.h"

#include "project_check.h"
#include "project_export.h"

#include <engine/log.h>
#include <engine/project/sdk_manifest.h>

#include <utility>
#include <vector>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}

BatchRun::BatchRun(engine::IProcessLauncher& processes, LineSink sink)
    : build_(processes)
    , sink_(std::move(sink)) {}

std::optional<int> BatchRun::start(BatchSetup setup) {
    std::optional<engine::SdkManifest> sdk;
    if (auto manifest = engine::read_sdk_manifest(setup.sdk)) {
        sdk = std::move(*manifest);
        sink_("SDK " + sdk->version + " (" + sdk->config + ") at " + path_text(setup.sdk));
    } else {
        engine::log::warn("Editor: not an installed SDK: " + engine::describe(manifest.error()));
        sink_("Not an installed SDK (" + path_text(setup.sdk) + "): " + engine::describe(manifest.error()));
    }
    const ProjectCheck check = check_project(setup.project, sdk);
    if (!check.fits) {
        return fail(check.problem);
    }
    const engine::WindProject& project = *check.project;
    export_directory_ = setup.export_directory.value_or(default_export_directory(check.directory, project.target));
    if (const std::string problem = export_directory_problem(check.directory, export_directory_); !problem.empty()) {
        return fail(problem);
    }
    sink_("Exporting " + project.name + " (" + project.target + ", Release) to " + path_text(export_directory_));
    build_.start(BuildSetup{
            .kind = BuildKind::Export,
            .project = check.directory,
            .sdk = setup.sdk,
            .target = project.target,
            .sdk_config = sdk->config,
    });
    running_ = true;
    return std::nullopt;
}

std::optional<int> BatchRun::poll() {
    if (!running_) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    const std::optional<BuildOutcome> outcome = build_.poll(lines);
    for (const std::string& line : lines) {
        sink_(line);
    }
    if (!outcome) {
        return std::nullopt;
    }
    running_ = false;
    if (!outcome->has_value()) {
        return fail(outcome->error());
    }
    const auto copied = copy_export(**outcome, export_directory_);
    if (!copied) {
        return fail(copied.error());
    }
    sink_("Exported to " + path_text(*copied));
    return kBatchOk;
}

int BatchRun::fail(const std::string& message) {
    engine::log::error("Editor: " + message);
    sink_("error: " + message);
    return kBatchFailed;
}

}
