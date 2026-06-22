#include "launcher_app.h"

#include "user_paths.h"

#include <asset_ids.h>

#include <engine/core/platform.h>
#include <engine/core/window_control.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/log.h>
#include <engine/process/process_launcher.h>
#include <engine/ui/canvas.h>

#include <algorithm>
#include <expected>
#include <string_view>
#include <system_error>
#include <utility>

namespace launcher {
namespace {

constexpr char kOrganization[] = "Wind";
constexpr char kApplication[] = "Launcher";
constexpr char kStateFile[] = "launcher.txt";
constexpr char kMissingTone[] = "#ff6b68";

std::string sdk_detail(const SdkEntry& sdk) {
    std::string detail = sdk.manifest.config;
    if (sdk.manifest.dirty) {
        detail += ", local changes";
    }
    detail += ", commit " + sdk.manifest.commit.substr(0, 8);
    detail += sdk.located ? ", located" : ", installed";
    return detail;
}

bool has_template(const SdkEntry& sdk) {
    std::error_code error;
    return std::filesystem::is_directory(project_template(sdk), error);
}

std::string error_text(engine::ProcessError error, std::string_view program) {
    switch (error) {
        case engine::ProcessError::NotFound:
            return std::string(program) + " is missing";
        case engine::ProcessError::StartFailed:
            return std::string(program) + " could not start";
        case engine::ProcessError::Unsupported:
            return "starting programs is not supported on this platform";
    }
    return std::string(program) + " could not start";
}

}

LauncherApp::LauncherApp(const engine::EngineServices& services)
    : engine::GameBase(services.worlds)
    , windows_(&services.windows)
    , processes_(&services.processes)
    , view_model_(std::make_shared<LauncherViewModel>()) {
    view_model_->showProjects.bind_to<LauncherApp, &LauncherApp::show_projects>(*this);
    view_model_->showSdks.bind_to<LauncherApp, &LauncherApp::show_sdks>(*this);
    view_model_->addProject.bind_to<LauncherApp, &LauncherApp::add_project>(*this);
    view_model_->locateSdk.bind_to<LauncherApp, &LauncherApp::locate_sdk>(*this);
    view_model_->showSdkFolder.bind_to<LauncherApp, &LauncherApp::show_sdk_folder>(*this);
    view_model_->newProject.bind_to<LauncherApp, &LauncherApp::new_project>(*this);
    view_model_->browseLocation.bind_to<LauncherApp, &LauncherApp::browse_location>(*this);
    view_model_->toggleSdkPicker.bind_to<LauncherApp, &LauncherApp::toggle_sdk_picker>(*this);
    view_model_->createProject.bind_to<LauncherApp, &LauncherApp::create_project, &LauncherApp::can_create_project>(
            *this);
    view_model_->cancelNewProject.bind_to<LauncherApp, &LauncherApp::cancel_new_project>(*this);
}

engine::WindowDesc LauncherApp::primary_window() const {
    return engine::WindowDesc{.title = "Wind Launcher", .size = {960, 620}};
}

void LauncherApp::on_start() {
    engine::ecs::World& ui = world();
    const engine::ecs::Entity canvas = ui.create();
    ui.emplace<engine::ui::UiCanvas>(canvas, engine::ui::UiCanvas{
            .document = assets::ui::launcher,
            .stylesheet = assets::css::launcher,
            .data_context = view_model_,
            .fit = engine::ui::UiFit::FillWindow,
    });
    ui.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World&) { frame(); });

    if (auto directory = engine::user_data_directory(kOrganization, kApplication)) {
        data_dir_ = std::move(*directory);
        state_ = load_launcher_state(data_dir_ / kStateFile);
    } else {
        engine::log::warn("Launcher: no user data directory (" + directory.error().message() +
                "). Nothing is remembered");
    }
    install_dir_ = sdk_install_directory();
    if (install_dir_.empty()) {
        engine::log::warn("Launcher: no SDK install directory. Only located SDKs are listed");
        view_model_->sdkFolderText = std::string("No install directory: locate an SDK by its sdk.toml.");
    } else {
        remove_deleted_sdks(install_dir_);
        view_model_->sdkFolderText = "Installed in " + path_text(install_dir_);
    }
    refresh();
    if (projects_.empty()) {
        show_status("Make a project with New project, or add one you have with Add existing.");
    }
}

void LauncherApp::show_projects() {
    show_page(Page::Projects);
}

void LauncherApp::show_sdks() {
    show_page(Page::Sdks);
}

void LauncherApp::show_page(Page page) {
    page_ = page;
    const auto display = [&](Page shown) { return std::string(page == shown ? "block" : "none"); };
    view_model_->projectsTab = page != Page::Sdks;
    view_model_->sdksTab = page == Page::Sdks;
    view_model_->projectsDisplay = display(Page::Projects);
    view_model_->sdksDisplay = display(Page::Sdks);
    view_model_->newProjectDisplay = display(Page::NewProject);
    view_model_->sdkPickerOpen = false;
}

void LauncherApp::add_project() {
    request(LauncherRequest{.kind = LauncherRequest::Kind::AddProject});
}

void LauncherApp::new_project() {
    std::filesystem::path location = state_.location.empty() ? default_project_location() : state_.location;
    view_model_->newLocation = location.empty() ? std::string{} : path_text(location);
    // "My Game", or "My Game 2" and on when that directory is taken.
    std::string name = "My Game";
    for (int n = 2; n < 100 && new_project_problem(NewProject{.name = name, .location = location}); ++n) {
        name = "My Game " + std::to_string(n);
    }
    view_model_->newName = name;
    new_sdk_root_.clear();
    refresh_sdk_options();
    show_page(Page::NewProject);
    check_new_project();
}

void LauncherApp::browse_location() {
    request(LauncherRequest{.kind = LauncherRequest::Kind::BrowseLocation});
}

void LauncherApp::toggle_sdk_picker() {
    view_model_->sdkPickerOpen = !view_model_->sdkPickerOpen.get();
}

void LauncherApp::pick_new_project_sdk(std::size_t index) {
    view_model_->sdkPickerOpen = false;
    if (index < sdks_.size()) {
        new_sdk_root_ = sdks_[index].root;
        refresh_sdk_options();
        check_new_project();
    }
}

void LauncherApp::create_project() {
    request(LauncherRequest{.kind = LauncherRequest::Kind::CreateProject});
}

bool LauncherApp::can_create_project() const {
    return page_ == Page::NewProject && !new_project_problem_;
}

void LauncherApp::cancel_new_project() {
    show_page(Page::Projects);
}

void LauncherApp::locate_sdk() {
    request(LauncherRequest{.kind = LauncherRequest::Kind::LocateSdk});
}

void LauncherApp::show_sdk_folder() {
    request(LauncherRequest{.kind = LauncherRequest::Kind::ShowSdkFolder});
}

void LauncherApp::request(LauncherRequest request) {
    request_ = request;
}

void LauncherApp::frame() {
    if (const std::optional<engine::FileDialogResult> answer = dialog_.take()) {
        take_dialog_answer(*answer);
    }
    act(std::exchange(request_, LauncherRequest{}));
    if (page_ == Page::NewProject) {
        check_new_project();
    }
}

void LauncherApp::act(const LauncherRequest& request) {
    switch (request.kind) {
        case LauncherRequest::Kind::None:
            break;
        case LauncherRequest::Kind::AddProject:
            open_dialog(DialogPurpose::AddProject);
            break;
        case LauncherRequest::Kind::LocateSdk:
            open_dialog(DialogPurpose::LocateSdk);
            break;
        case LauncherRequest::Kind::ShowSdkFolder:
            if (!install_dir_.empty()) {
                std::error_code error;
                std::filesystem::create_directories(install_dir_, error);
                show_folder(install_dir_);
            }
            break;
        case LauncherRequest::Kind::BrowseLocation:
            open_dialog(DialogPurpose::ProjectLocation);
            break;
        case LauncherRequest::Kind::CreateProject:
            make_project();
            break;
        case LauncherRequest::Kind::Open:
            open_project(request.index);
            break;
        case LauncherRequest::Kind::Remove:
            if (request.index < projects_.size()) {
                forget_project(state_, projects_[request.index].directory);
                refresh();
                show_status("Removed from the list. The project's files are untouched.");
            }
            break;
        case LauncherRequest::Kind::ShowSdk:
            if (request.index < sdks_.size()) {
                show_folder(sdks_[request.index].root);
            }
            break;
        case LauncherRequest::Kind::ForgetSdk:
            if (request.index < sdks_.size() && sdks_[request.index].located) {
                forget_sdk(state_, sdks_[request.index].root);
                refresh();
                show_status("Forgot the SDK. Its files are untouched.");
            }
            break;
        case LauncherRequest::Kind::DeleteSdk:
            delete_sdk(request.index);
            break;
    }
}

void LauncherApp::open_dialog(DialogPurpose purpose) {
    if (dialog_.pending()) {
        return;
    }
    dialog_purpose_ = purpose;
    if (purpose == DialogPurpose::ProjectLocation) {
        dialog_ = windows_->request_open_folder(
                engine::kPrimaryWindow, path_from_text(view_model_->newLocation.get()));
        return;
    }
    std::vector<engine::FileFilter> filters{
            purpose == DialogPurpose::AddProject
                    ? engine::FileFilter{.name = "Wind project (wind_project.toml)", .pattern = "toml"}
                    : engine::FileFilter{.name = "Wind editor SDK (sdk.toml)", .pattern = "toml"},
    };
    dialog_ = windows_->request_open_file(engine::kPrimaryWindow, std::move(filters));
}

void LauncherApp::take_dialog_answer(const engine::FileDialogResult& answer) {
    if (!answer.path) {
        return;
    }
    const std::filesystem::path& file = *answer.path;
    if (dialog_purpose_ == DialogPurpose::ProjectLocation) {
        view_model_->newLocation = path_text(file);
        return;
    }
    if (dialog_purpose_ == DialogPurpose::AddProject) {
        if (file.filename() != engine::kWindProjectFile) {
            show_status("Pick the project's wind_project.toml.");
            return;
        }
        remember_project(state_, file.parent_path());
        refresh();
        show_status(projects_.front().project ? "Added " + projects_.front().project->name + "."
                                              : projects_.front().problem);
        return;
    }
    if (file.filename() != engine::kSdkManifestFile) {
        show_status("Pick the SDK's sdk.toml (beside its bin/).");
        return;
    }
    const auto manifest = engine::read_sdk_manifest(file.parent_path());
    if (!manifest) {
        show_status(engine::describe(manifest.error()));
        return;
    }
    remember_sdk(state_, file.parent_path());
    refresh();
    show_status("Located SDK " + manifest->version + ".");
}

void LauncherApp::open_project(std::size_t index, const SdkEntry* in_sdk) {
    if (index >= projects_.size() || !projects_[index].project) {
        return;
    }
    const ProjectEntry entry = projects_[index];
    const SdkEntry* sdk = in_sdk != nullptr ? in_sdk : sdk_for(sdks_, entry.project->engine);
    if (sdk == nullptr) {
        show_status(entry.project->name + " needs SDK " + entry.project->engine + ", which is not installed.");
        return;
    }
    const std::expected<void, engine::ProcessError> launched = processes_->launch(editor_launch(*sdk, entry.directory));
    if (!launched) {
        show_status("Could not open " + entry.project->name + ": " + error_text(launched.error(), "the editor") +
                " (" + path_text(editor_executable(*sdk)) + ").");
        return;
    }
    // refresh() rebuilds sdks_, which `sdk` points into: keep what the status line needs first.
    const std::string version = sdk->manifest.version;
    engine::log::info("Launcher: opened " + path_text(entry.directory) + " in editor " + version + " at " +
            path_text(sdk->root));
    remember_project(state_, entry.directory);
    refresh();
    show_status("Opened " + entry.project->name + " in editor " + version + ".");
}

void LauncherApp::delete_sdk(std::size_t index) {
    // Only an SDK found under the install directory: a located one is the user's own build, which Forget leaves be.
    if (index >= sdks_.size() || sdks_[index].located) {
        return;
    }
    const SdkEntry sdk = sdks_[index];
    const std::expected<void, std::string> deleted = launcher::delete_sdk(sdk.root);
    engine::log::info("Launcher: delete SDK " + sdk.manifest.version + " at " + path_text(sdk.root) + ": " +
            (deleted ? std::string("done") : deleted.error()));
    refresh();
    show_status(deleted ? "Deleted SDK " + sdk.manifest.version + "." : deleted.error());
}

void LauncherApp::show_folder(const std::filesystem::path& directory) {
    const std::expected<void, engine::ProcessError> launched = processes_->launch(folder_launch(directory));
    if (!launched) {
        show_status("Could not show " + path_text(directory) + ": " +
                error_text(launched.error(), "the file manager") + ".");
    }
}

const SdkEntry* LauncherApp::picked_sdk() const {
    const auto sdk = std::ranges::find_if(sdks_, [&](const SdkEntry& entry) { return entry.root == new_sdk_root_; });
    return sdk == sdks_.end() ? nullptr : &*sdk;
}

NewProject LauncherApp::new_project_form() const {
    NewProject project{
            .name = view_model_->newName.get(),
            .location = path_from_text(view_model_->newLocation.get()),
    };
    if (const SdkEntry* sdk = picked_sdk()) {
        project.sdk_root = sdk->root;
        project.engine = sdk->manifest.version;
    }
    return project;
}

void LauncherApp::check_new_project() {
    const NewProject project = new_project_form();
    const SdkEntry* sdk = picked_sdk();
    new_project_problem_ = new_project_problem(project);
    if (!new_project_problem_ && sdk == nullptr) {
        new_project_problem_ = "Install or locate an SDK first (SDKs page).";
    } else if (!new_project_problem_ && !has_template(*sdk)) {
        new_project_problem_ = "SDK " + project.engine +
                               " has no project template. Install an SDK built from a newer engine.";
    }
    if (new_project_problem_) {
        view_model_->newHint = *new_project_problem_;
        view_model_->newHintTone = std::string(kMissingTone);
    } else {
        view_model_->newHint = "Creates " + path_text(project_directory(project)) + ", build target " +
                               project_target(project.name) + ", and opens it in the editor.";
        view_model_->newHintTone = std::string{};
    }
}

void LauncherApp::make_project() {
    check_new_project();
    if (page_ != Page::NewProject || new_project_problem_) {
        return;
    }
    const NewProject project = new_project_form();
    const std::expected<std::filesystem::path, std::string> created =
            launcher::create_project(project_template(*picked_sdk()), project);
    if (!created) {
        show_status(created.error());
        return;
    }
    engine::log::info("Launcher: created " + path_text(*created) + " from SDK " + project.engine);
    state_.location = project.location;
    remember_project(state_, *created);
    refresh();
    show_page(Page::Projects);
    // In the SDK picked, not whichever of that version Open would prefer: a dirty dev SDK loses to a clean one there.
    open_project(0, picked_sdk());
}

void LauncherApp::refresh_sdk_options() {
    if (picked_sdk() == nullptr) {
        // The newest SDK that can make projects, else the newest.
        const auto usable = std::ranges::find_if(sdks_, has_template);
        new_sdk_root_ = usable != sdks_.end() ? usable->root : sdks_.empty() ? std::filesystem::path{} : sdks_[0].root;
    }

    std::vector<std::shared_ptr<SdkOptionViewModel>> options;
    view_model_->newSdkText = std::string("No SDK");
    for (std::size_t i = 0; i < sdks_.size(); ++i) {
        auto option = std::make_shared<SdkOptionViewModel>(*this, i);
        std::string text = sdks_[i].manifest.version + "   " + sdk_detail(sdks_[i]);
        if (!has_template(sdks_[i])) {
            text += ", no template";
        }
        option->selected = sdks_[i].root == new_sdk_root_;
        if (option->selected.get()) {
            view_model_->newSdkText = text;
        }
        option->text = std::move(text);
        options.push_back(std::move(option));
    }
    view_model_->sdkOptions.set(std::move(options));
}

void LauncherApp::refresh() {
    std::vector<std::string> problems;
    sdks_ = find_sdks(install_dir_, state_.sdks, problems);
    for (const std::string& problem : problems) {
        engine::log::warn("Launcher: " + problem);
    }
    projects_.clear();
    for (const std::filesystem::path& directory : state_.projects) {
        projects_.push_back(read_project_entry(directory));
    }

    std::vector<std::shared_ptr<ProjectRowViewModel>> project_rows;
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        const ProjectEntry& entry = projects_[i];
        const SdkEntry* sdk = entry.project ? sdk_for(sdks_, entry.project->engine) : nullptr;
        auto row = std::make_shared<ProjectRowViewModel>(*this, i, sdk != nullptr);
        row->path = path_text(entry.directory);
        if (entry.project) {
            row->name = entry.project->name;
            row->engineText = (sdk != nullptr ? "SDK " : "Needs SDK ") + entry.project->engine;
            row->tone = std::string(sdk != nullptr ? "" : kMissingTone);
        } else {
            row->name = path_text(entry.directory.filename());
            row->engineText = std::string("Cannot read");
            row->tone = std::string(kMissingTone);
        }
        project_rows.push_back(std::move(row));
    }
    view_model_->projects.set(std::move(project_rows));

    std::vector<std::shared_ptr<SdkRowViewModel>> sdk_rows;
    for (std::size_t i = 0; i < sdks_.size(); ++i) {
        auto row = std::make_shared<SdkRowViewModel>(*this, i, sdks_[i].located);
        row->version = sdks_[i].manifest.version;
        row->detail = sdk_detail(sdks_[i]);
        row->path = path_text(sdks_[i].root);
        sdk_rows.push_back(std::move(row));
    }
    view_model_->sdks.set(std::move(sdk_rows));
    refresh_sdk_options();

    if (!data_dir_.empty() && !save_launcher_state(data_dir_ / kStateFile, state_)) {
        engine::log::warn("Launcher: could not save " + path_text(data_dir_ / kStateFile));
    }
}

void LauncherApp::show_status(std::string text) {
    view_model_->statusText = std::move(text);
}

}
