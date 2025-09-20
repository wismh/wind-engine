#include "batch_run.h"
#include "editor_app.h"
#include "editor_options.h"

#include <engine/core/platform.h>
#include <engine/process/process_launcher.h>

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace {

// --batch: no window and no EngineHost. cmake runs through the process launcher, which this loop polls; the lines go
// to standard output and to --log-file. A Windows GUI-subsystem process has no console of its own, so a caller that
// needs the lines reads the log file and waits for the exit code (Start-Process -Wait).
int run_batch(const editor::EditorOptions& options) {
    std::ofstream log_file;
    if (options.log_file) {
        log_file.open(*options.log_file, std::ios::out | std::ios::trunc);
        if (!log_file) {
            std::cerr << "Cannot write the log file " << options.log_file->string() << std::endl;
            return editor::kBatchFailed;
        }
    }
    const auto write = [&](const std::string& line) {
        std::cout << line << std::endl;
        if (log_file.is_open()) {
            log_file << line << std::endl;
        }
    };

    engine::ProcessLauncher processes;
    editor::BatchRun run(processes, write);
    // `<sdk>/bin/wind_editor` -> `<sdk>`.
    std::optional<int> code = run.start(editor::BatchSetup{
            .sdk = engine::executable_directory().parent_path(),
            .project = *options.project,
            .export_directory = options.export_directory,
    });
    while (!code) {
        processes.poll();
        code = run.poll();
        if (!code) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return *code;
}

}

int main(int argc, char** argv) {
    const editor::EditorOptions options =
            editor::parse_editor_options(std::span<char* const>(argv + 1, static_cast<std::size_t>(argc - 1)));
    if (const std::string problem = editor::usage_error(options); !problem.empty()) {
        std::cerr << problem << std::endl;
        return editor::kBatchUsage;
    }
    if (options.batch) {
        return run_batch(options);
    }
    editor::EditorApp app;
    return app.run(options);
}
