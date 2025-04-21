#include "toolbar.h"

#include <utility>

namespace editor {

Toolbar::Toolbar() : view_model_(std::make_shared<EditorViewModel>()) {
    view_model_->chooseGame.bind_to<Toolbar, &Toolbar::choose_game, &Toolbar::can_choose_game>(*this);
    view_model_->togglePlay.bind_to<Toolbar, &Toolbar::toggle_play, &Toolbar::can_toggle_play>(*this);
    view_model_->showInspector.bind_to<Toolbar, &Toolbar::show_inspector>(*this);
    view_model_->showProfiler.bind_to<Toolbar, &Toolbar::show_profiler>(*this);
    view_model_->gamePath = std::string("No game chosen");
}

const std::shared_ptr<EditorViewModel>& Toolbar::view_model() const {
    return view_model_;
}

void Toolbar::choose_game() {
    request_ = EditorRequest::ChooseGame;
}

void Toolbar::toggle_play() {
    request_ = playing_ ? EditorRequest::Stop : EditorRequest::Play;
}

void Toolbar::show_inspector() {
    show_tab(EditorTab::Inspector);
}

void Toolbar::show_profiler() {
    show_tab(EditorTab::Profiler);
}

bool Toolbar::can_choose_game() const {
    return !playing_;
}

bool Toolbar::can_toggle_play() const {
    return playing_ || has_game_;
}

void Toolbar::show_game(const std::filesystem::path& module) {
    has_game_ = !module.empty();
    const std::u8string text = module.u8string();
    view_model_->gamePath = has_game_ ? std::string(reinterpret_cast<const char*>(text.data()), text.size())
                                      : std::string("No game chosen");
}

void Toolbar::show_status(std::string text) {
    view_model_->statusText = std::move(text);
}

void Toolbar::show_playing(bool playing) {
    playing_ = playing;
    view_model_->isPlaying = playing;
    view_model_->playLabel = std::string(playing ? "Stop" : "Play");
}

EditorRequest Toolbar::take_request() {
    return std::exchange(request_, EditorRequest::None);
}

EditorTab Toolbar::active_tab() const {
    return tab_;
}

void Toolbar::show_tab(EditorTab tab) {
    tab_ = tab;
    view_model_->inspectorTab = tab == EditorTab::Inspector;
    view_model_->profilerTab = tab == EditorTab::Profiler;
}

}
