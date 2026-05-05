#include "export_dialog.h"

#include "project_build.h"
#include "project_export.h"

#include <system_error>
#include <utility>

namespace editor {
namespace {

constexpr char kProblemTone[] = "#ff6b68";
constexpr char kWarningTone[] = "#e5b455";

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path path_from(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string trimmed(const std::string& text) {
    constexpr char kSpace[] = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(kSpace);
    if (begin == std::string::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(kSpace) - begin + 1);
}

}

ExportDialog::ExportDialog()
    : view_model_(std::make_shared<ExportViewModel>()) {
    view_model_->browse.bind_to<ExportDialog, &ExportDialog::browse>(*this);
    view_model_->confirm.bind_to<ExportDialog, &ExportDialog::confirm, &ExportDialog::can_confirm>(*this);
    view_model_->cancel.bind_to<ExportDialog, &ExportDialog::cancel>(*this);
}

const std::shared_ptr<ExportViewModel>& ExportDialog::view_model() const {
    return view_model_;
}

void ExportDialog::show(const std::filesystem::path& project, const std::string& target, const std::string& sdk_config) {
    project_ = project;
    target_ = target;
    sdk_config_ = sdk_config;
    if (last_project_ != project_) {
        last_project_ = project_;
        last_path_ = path_text(default_export_directory(project_, target_));
        last_development_ = false;
    }
    choice_.reset();
    close_requested_ = false;
    view_model_->path = last_path_;
    view_model_->development = last_development_;
    is_open_ = true;
    refresh_message();
}

void ExportDialog::close() {
    is_open_ = false;
}

bool ExportDialog::is_open() const {
    return is_open_;
}

void ExportDialog::set_available(bool available) {
    available_ = available;
}

void ExportDialog::frame(engine::IWindowControl& windows, engine::WindowId owner) {
    if (browse_requested_ && !folder_call_.pending()) {
        browse_requested_ = false;
        // The platform opens the dialog in the nearest folder of the text that exists.
        std::filesystem::path start = chosen_directory();
        std::error_code error;
        while (!start.empty() && !std::filesystem::is_directory(start, error)) {
            std::filesystem::path parent = start.parent_path();
            start = parent == start ? std::filesystem::path{} : std::move(parent);
        }
        folder_call_ = windows.request_open_folder(owner, std::move(start));
    }
    if (const std::optional<engine::FileDialogResult> answer = folder_call_.take(); answer && answer->path) {
        last_path_ = path_text(*answer->path);
        view_model_->path = last_path_;
    }
    if (!is_open()) {
        return;
    }
    last_path_ = view_model_->path.get();
    last_development_ = view_model_->development.get();
    if (last_path_ != checked_path_ || last_development_ != checked_development_) {
        refresh_message();
    }
}

void ExportDialog::browse() {
    browse_requested_ = true;
}

void ExportDialog::confirm() {
    if (!is_open() || !available_ || project_.empty()) {
        return;
    }
    // The text may have changed since the last frame checked it.
    refresh_message();
    if (problem_) {
        return;
    }
    choice_ = ExportChoice{.directory = chosen_directory(), .development = view_model_->development.get()};
    close_requested_ = true;
    close();
}

bool ExportDialog::can_confirm() const {
    return is_open() && available_ && !project_.empty() && !problem_;
}

void ExportDialog::cancel() {
    close_requested_ = true;
    close();
}

std::optional<ExportChoice> ExportDialog::take_choice() {
    return std::exchange(choice_, std::nullopt);
}

bool ExportDialog::take_close_requested() {
    return std::exchange(close_requested_, false);
}

const std::string& ExportDialog::message() const {
    return view_model_->message.get();
}

bool ExportDialog::has_problem() const {
    return problem_;
}

std::filesystem::path ExportDialog::chosen_directory() const {
    const std::string text = trimmed(view_model_->path.get());
    if (text.empty()) {
        return {};
    }
    const std::filesystem::path directory = path_from(text);
    return directory.is_absolute() ? directory : project_ / directory;
}

void ExportDialog::refresh_message() {
    checked_path_ = view_model_->path.get();
    checked_development_ = view_model_->development.get();
    std::string message;
    problem_ = false;
    if (trimmed(checked_path_).empty()) {
        message = "Choose the folder to export the game to.";
        problem_ = true;
    } else {
        message = export_directory_problem(project_, chosen_directory());
        if (message.empty()) {
            message = export_flavour_problem(sdk_config_, checked_development_);
        }
        problem_ = !message.empty();
        std::error_code error;
        if (!problem_ && std::filesystem::exists(chosen_directory() / kExportMarker, error)) {
            message = "An earlier export is in this folder. Export replaces it.";
        }
    }
    view_model_->message = message;
    view_model_->messageTone = std::string(problem_ ? kProblemTone : message.empty() ? "" : kWarningTone);
}

}
