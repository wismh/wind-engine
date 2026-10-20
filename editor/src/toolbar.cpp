#include "toolbar.h"

#include <utility>

namespace editor {

Toolbar::Toolbar()
    : view_model_(std::make_shared<EditorViewModel>()) {
    view_model_->togglePlay.bind_to<Toolbar, &Toolbar::toggle_play, &Toolbar::can_toggle_play>(*this);
    view_model_->refreshProject.bind_to<Toolbar, &Toolbar::refresh_project, &Toolbar::can_refresh_project>(*this);
    view_model_->exportGame.bind_to<Toolbar, &Toolbar::export_game, &Toolbar::can_export_game>(*this);
    view_model_->minimizeWindow.bind_to<Toolbar, &Toolbar::minimize_window, &Toolbar::can_window_action>(*this);
    view_model_->maximizeWindow.bind_to<Toolbar, &Toolbar::maximize_window, &Toolbar::can_window_action>(*this);
    view_model_->closeWindow.bind_to<Toolbar, &Toolbar::close_window, &Toolbar::can_window_action>(*this);
    view_model_->playLabel = std::string("\xe2\x96\xb6 Play");
}

void Toolbar::set_window_control(engine::IWindowControl& control, engine::WindowId window) {
    window_control_ = &control;
    window_ = window;
    update_window_state();
}

void Toolbar::update_window_state() {
    if (window_control_ != nullptr) {
        const bool max = window_control_->is_maximized(window_);
        view_model_->isMaximized = max;
        // In Inter font, \u25a2 (white square with rounded corners or nested) and \u25a1 (white square) render crisply
        view_model_->maxRestoreLabel = max ? std::string("\xe2\x96\xa2") : std::string("\xe2\x96\xa1");
    }
}

void Toolbar::minimize_window() {
    if (window_control_ != nullptr) {
        window_control_->minimize(window_);
    }
}

void Toolbar::maximize_window() {
    if (window_control_ != nullptr) {
        if (window_control_->is_maximized(window_)) {
            window_control_->restore(window_);
        } else {
            window_control_->maximize(window_);
        }
        update_window_state();
    }
}

void Toolbar::close_window() {
    request_ = EditorRequest::Quit;
}

bool Toolbar::can_window_action() const {
    return true;
}

const std::shared_ptr<EditorViewModel>& Toolbar::view_model() const {
    return view_model_;
}

void Toolbar::toggle_play() {
    request_ = state_ == RunState::Idle ? EditorRequest::Play : EditorRequest::Stop;
}

bool Toolbar::can_toggle_play() const {
    return state_ != RunState::Idle || playable_;
}

void Toolbar::refresh_project() {
    request_ = EditorRequest::Refresh;
}

bool Toolbar::can_refresh_project() const {
    // show_project("") is no project. A name is set whenever the Project tab is open.
    return !view_model_->projectName.get().empty();
}

void Toolbar::export_game() {
    request_ = EditorRequest::Export;
}

bool Toolbar::can_export_game() const {
    return state_ == RunState::Idle && playable_;
}

void Toolbar::show_project(std::string text, bool playable) {
    playable_ = playable;
    view_model_->projectName = std::move(text);
}

void Toolbar::show_status(std::string text) {
    status_ = std::move(text);
}

void Toolbar::show_state(RunState state) {
    state_ = state;
    view_model_->isPlaying = state != RunState::Idle;
    const char* label = state == RunState::Idle ? "\xe2\x96\xb6 Play" : "\xe2\x96\xa0 Stop";
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
    return status_;
}

}
