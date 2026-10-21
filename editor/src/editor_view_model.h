#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// Fields of assets/ui/editor.xml. Names are the XML binding paths, so they stay camelCase.
class EditorViewModel final : public engine::ui::ViewModel {
public:
    EditorViewModel();

    engine::ui::Bindable<std::string> projectName;
    engine::ui::Bindable<std::string> playLabel{std::string("\xe2\x96\xb6 Play")};
    engine::ui::Bindable<bool> isPlaying{false};

    engine::ui::Bindable<std::string> windowTitle{std::string("Wind Editor")};
    engine::ui::Bindable<std::string> maxRestoreLabel{std::string("\xe2\x96\xa1")};
    engine::ui::Bindable<bool> isMaximized{false};

    engine::ui::MethodCommand undo;
    engine::ui::MethodCommand redo;

    engine::ui::MethodCommand togglePlay;
    engine::ui::MethodCommand refreshProject;
    engine::ui::MethodCommand exportGame;

    engine::ui::MethodCommand showProject;
    engine::ui::MethodCommand showHierarchy;
    engine::ui::MethodCommand showWorld;
    engine::ui::MethodCommand showUiTree;
    engine::ui::MethodCommand showInspector;
    engine::ui::MethodCommand showProfiler;
    engine::ui::MethodCommand showBuild;

    engine::ui::MethodCommand minimizeWindow;
    engine::ui::MethodCommand maximizeWindow;
    engine::ui::MethodCommand closeWindow;
};

}
