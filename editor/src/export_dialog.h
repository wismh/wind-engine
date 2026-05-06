#pragma once

#include "export_view_model.h"

#include <engine/core/file_dialog.h>
#include <engine/core/window_control.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace editor {

// What the Export dialog asks for.
struct ExportChoice {
    // Where the game goes: absolute (a relative folder the user typed is taken from the project).
    std::filesystem::path directory;
    // The Development build (DebugGame, with the profiler and wind-cli) instead of Release.
    bool development = false;
};

// The panel opened by File -> Export: a dock panel floating in its own window with the target folder (a text field
// and Browse...), the Development build checkbox, a line about the folder (why it cannot be used, or that it
// replaces an earlier export), and Export and Cancel buttons. Owns the logic and its ExportViewModel.
// Holds `this` in its commands, so it never moves.
class ExportDialog {
public:
    ExportDialog();

    ExportDialog(const ExportDialog&) = delete;
    ExportDialog& operator=(const ExportDialog&) = delete;

    [[nodiscard]] const std::shared_ptr<ExportViewModel>& view_model() const;

    // Opens the dialog for `project` (its directory and engine_add_game target) against an SDK of `sdk_config`.
    void show(const std::filesystem::path& project, const std::string& target, const std::string& sdk_config);
    void close();
    [[nodiscard]] bool is_open() const;

    // Whether Export may start a build: nothing else runs, and the project fits the SDK.
    void set_available(bool available);

    // Once per frame, after the UI pass: asks for the folder dialog Browse... wanted, takes its answer, and checks the
    // folder text when it changed. `owner` is the editor's window.
    void frame(engine::IWindowControl& windows, engine::WindowId owner);

    void browse();
    void confirm();
    [[nodiscard]] bool can_confirm() const;
    void cancel();

    // The choice Export recorded, once.
    [[nodiscard]] std::optional<ExportChoice> take_choice();
    [[nodiscard]] bool take_close_requested();

    // The line shown under the checkbox, and whether it is a problem (Export disabled) or only a warning.
    [[nodiscard]] const std::string& message() const;
    [[nodiscard]] bool has_problem() const;

private:
    void refresh_message();
    [[nodiscard]] std::filesystem::path chosen_directory() const;

    std::shared_ptr<ExportViewModel> view_model_;
    std::filesystem::path project_;
    std::string target_;
    std::string sdk_config_;
    bool is_open_ = false;
    bool close_requested_ = false;
    bool available_ = false;
    bool browse_requested_ = false;
    engine::FileDialogCall folder_call_;
    // The folder text and checkbox the message was worked out for.
    std::string checked_path_;
    bool checked_development_ = false;
    bool problem_ = false;
    std::optional<ExportChoice> choice_;
    // The last folder and checkbox of this session, and the project they were for.
    std::filesystem::path last_project_;
    std::string last_path_;
    bool last_development_ = false;
};

}
