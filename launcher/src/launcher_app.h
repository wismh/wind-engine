#pragma once

#include "launcher_request.h"
#include "launcher_state.h"
#include "launcher_view_model.h"
#include "project_entry.h"
#include "sdk_catalog.h"

#include <engine/core/engine_services.h>
#include <engine/core/file_dialog.h>
#include <engine/igame.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace launcher {

// Wind Launcher: the projects the user works on and the editor SDKs installed on this machine. Open starts the editor
// whose version the project names, with `--project`. A Wind app like a game, built against the static engine, so it
// serves every SDK version.
//
// Remembers its lists in `user_data_directory("Wind", "Launcher")/launcher.txt` and finds installed SDKs under
// `sdks/` beside it. Holds `this` in its rows and its system, so it never moves.
class LauncherApp final : public engine::GameBase {
public:
    explicit LauncherApp(const engine::EngineServices& services);

    LauncherApp(const LauncherApp&) = delete;
    LauncherApp& operator=(const LauncherApp&) = delete;

    [[nodiscard]] engine::WindowDesc primary_window() const override;
    void on_start() override;

    // Header buttons.
    void add_project();
    void locate_sdk();

    // Row buttons. The last request since the previous frame wins.
    void request(LauncherRequest request);

private:
    enum class DialogPurpose {
        AddProject,
        LocateSdk,
    };

    void frame();
    void take_dialog_answer(const engine::FileDialogResult& answer);
    void act(const LauncherRequest& request);
    void open_dialog(DialogPurpose purpose);
    void open_project(std::size_t index);
    // Reads every project and SDK again, rebuilds the rows, and saves the lists.
    void refresh();
    void show_status(std::string text);

    engine::IWindowControl* windows_;
    engine::IProcessLauncher* processes_;
    std::shared_ptr<LauncherViewModel> view_model_;

    // Empty when there is no user data directory: nothing is remembered and no SDK is found by scanning.
    std::filesystem::path data_dir_;
    LauncherState state_;
    std::vector<ProjectEntry> projects_;
    std::vector<SdkEntry> sdks_;

    engine::FileDialogCall dialog_;
    DialogPurpose dialog_purpose_ = DialogPurpose::AddProject;
    LauncherRequest request_;
};

}
