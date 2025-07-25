#include "toolbar.h"

#include <utility>

namespace editor {

Toolbar::Toolbar() : view_model_(std::make_shared<EditorViewModel>()) {
    view_model_->togglePlay.bind_to<Toolbar, &Toolbar::toggle_play, &Toolbar::can_toggle_play>(*this);
    view_model_->showInspector.bind_to<Toolbar, &Toolbar::show_inspector>(*this);
    view_model_->showProfiler.bind_to<Toolbar, &Toolbar::show_profiler>(*this);
    view_model_->showBuild.bind_to<Toolbar, &Toolbar::show_build>(*this);
    view_model_->projectText = std::string("No project open");
}

const std::shared_ptr<EditorViewModel>& Toolbar::view_model() const {
    return view_model_;
}

void Toolbar::toggle_play() {
    request_ = state_ == RunState::Idle ? EditorRequest::Play : EditorRequest::Stop;
}

void Toolbar::show_inspector() {
    show_tab(EditorTab::Inspector);
}

void Toolbar::show_profiler() {
    show_tab(EditorTab::Profiler);
}

void Toolbar::show_build() {
    show_tab(EditorTab::Build);
}

bool Toolbar::can_toggle_play() const {
    return state_ != RunState::Idle || playable_;
}

void Toolbar::show_project(std::string text, bool playable) {
    playable_ = playable;
    view_model_->projectText = std::move(text);
}

void Toolbar::show_status(std::string text) {
    view_model_->statusText = std::move(text);
}

void Toolbar::show_state(RunState state) {
    state_ = state;
    view_model_->isPlaying = state != RunState::Idle;
    const char* label = "Play";
    if (state == RunState::Building) {
        label = "Cancel";
    } else if (state == RunState::Playing) {
        label = "Stop";
    }
    view_model_->playLabel = std::string(label);
}

EditorRequest Toolbar::take_request() {
    return std::exchange(request_, EditorRequest::None);
}

RunState Toolbar::state() const {
    return state_;
}

bool Toolbar::playable() const {
    return playable_;
}

const std::string& Toolbar::status() const {
    return view_model_->statusText.get();
}

EditorTab Toolbar::active_tab() const {
    return tab_;
}

void Toolbar::show_tab(EditorTab tab) {
    tab_ = tab;
    view_model_->inspectorTab = tab == EditorTab::Inspector;
    view_model_->profilerTab = tab == EditorTab::Profiler;
    view_model_->buildTab = tab == EditorTab::Build;
}

}
