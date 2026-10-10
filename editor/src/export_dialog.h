#pragma once

#include "editor_view_model.h"

#include <engine/core/file_dialog.h>
#include <engine/core/window_control.h>

#include <filesystem>
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

// The dialog the Export button opens: a popup beside the button with the target folder (a text field and Browse...), the
// Development build checkbox, a line about the folder (why it cannot be used, or that it replaces an earlier export),
// and Export and Cancel. Owns the logic; the view-model (EditorViewModel, `export*`) is only what the XML binds.
// Holds `this` in its commands, so it never moves.
//
// The folder starts as `<project>/export/<target>`, then as the last one this session used for the project. Export
// is enabled while the folder is usable and `available` (the editor builds one thing at a time). Confirming records the
// choice and closes the dialog; the editor takes the choice at the end of the frame.
class ExportDialog {
public:
    explicit ExportDialog(EditorViewModel& view_model);

    ExportDialog(const ExportDialog&) = delete;
    ExportDialog& operator=(const ExportDialog&) = delete;

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

    // The line shown under the checkbox, and whether it is a problem (Export disabled) or only a warning.
    [[nodiscard]] const std::string& message() const;
    [[nodiscard]] bool has_problem() const;

private:
    void refresh_message();
    [[nodiscard]] std::filesystem::path chosen_directory() const;

    EditorViewModel* view_model_;
    std::filesystem::path project_;
    std::string target_;
    std::string sdk_config_;
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
