#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <string>

namespace launcher {

class LauncherApp;

// One project row (`projects` in assets/ui/launcher.xml). The buttons ask the launcher for the row's index; the
// launcher acts at the end of its frame system, after the rows may have been rebuilt.
class ProjectRowViewModel final : public engine::ui::ViewModel {
public:
    ProjectRowViewModel(LauncherApp& app, std::size_t index, bool openable);

    ProjectRowViewModel(const ProjectRowViewModel&) = delete;
    ProjectRowViewModel& operator=(const ProjectRowViewModel&) = delete;

    void open_project();
    void remove_project();
    [[nodiscard]] bool can_open_project() const;

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<std::string> path;
    // "Editor 0.1.0", "Needs 0.2.0", or what is wrong with wind_project.toml.
    engine::ui::Bindable<std::string> engineText;
    // engineText's color through `var-tone`. Empty: the stylesheet's default.
    engine::ui::Bindable<std::string> tone;
    MethodCommand open;
    MethodCommand remove;

private:
    LauncherApp* app_;
    std::size_t index_;
    bool openable_;
};

}
