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

    engine::ui::Bindable<std::string> projectText;
    engine::ui::Bindable<std::string> statusText;
    engine::ui::Bindable<std::string> playLabel{std::string("Play")};
    engine::ui::Bindable<bool> isPlaying;

    // The Export dialog (ExportDialog): shown beside the Export button, the folder text the user types, the
    // Development build checkbox, and the problem or warning about the folder with its colour.
    engine::ui::Bindable<bool> exportOpen;
    engine::ui::Bindable<std::string> exportPath;
    engine::ui::Bindable<bool> exportDevelopment;
    engine::ui::Bindable<std::string> exportMessage;
    engine::ui::Bindable<std::string> exportMessageTone;

    engine::ui::Bindable<std::string> windowTitle{std::string("Wind Editor")};
    engine::ui::Bindable<std::string> maxRestoreLabel{std::string("\xe2\x96\xa1")};
    engine::ui::Bindable<bool> isMaximized{false};

    engine::ui::MethodCommand togglePlay;
    // The Export button: opens the dialog.
    engine::ui::MethodCommand exportGame;
    engine::ui::MethodCommand exportBrowse;
    engine::ui::MethodCommand exportConfirm;
    engine::ui::MethodCommand exportCancel;

    engine::ui::MethodCommand minimizeWindow;
    engine::ui::MethodCommand maximizeWindow;
    engine::ui::MethodCommand closeWindow;
};

}
