#include "build_panel.h"

#include <utility>

namespace editor {
namespace {

constexpr char kErrorTone[] = "#ff6b68";
constexpr char kWarningTone[] = "#e5c07b";

// Layout clamps the offset to the log's height, so this always lands on the last line.
constexpr float kScrollToEnd = 1.0e9f;

bool contains(std::string_view line, std::string_view part) {
    return line.find(part) != std::string_view::npos;
}

}

LineTone tone_of(std::string_view line) {
    // MSVC and MSBuild: "file(12,5): error C2065: ...", "x.obj : error LNK2019: ...", "...: warning C4244: ...".
    // CMake: "CMake Error at CMakeLists.txt:3 (find_package):".
    if (contains(line, ": error ") || contains(line, ": fatal error ") || line.starts_with("CMake Error")) {
        return LineTone::Error;
    }
    if (contains(line, ": warning ") || line.starts_with("CMake Warning")) {
        return LineTone::Warning;
    }
    return LineTone::Plain;
}

BuildPanel::BuildPanel()
    : view_model_(std::make_shared<BuildViewModel>()) {
    view_model_->summary = std::string("Press Play to build and run the project.");
}

const std::shared_ptr<BuildViewModel>& BuildPanel::view_model() const {
    return view_model_;
}

void BuildPanel::clear() {
    lines_.clear();
    first_error_.clear();
    view_model_->lines.set({});
    view_model_->logScroll = 0.0f;
}

void BuildPanel::append(std::vector<std::string> lines) {
    if (lines.empty()) {
        return;
    }
    for (std::string& text : lines) {
        const LineTone tone = tone_of(text);
        if (tone == LineTone::Error && first_error_.empty()) {
            first_error_ = text;
        }
        auto line = std::make_shared<BuildLineViewModel>();
        line->tone = std::string(tone == LineTone::Error ? kErrorTone : tone == LineTone::Warning ? kWarningTone : "");
        line->text = std::move(text);
        lines_.push_back(std::move(line));
    }
    if (lines_.size() > kMaxLines) {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMaxLines));
    }
    view_model_->lines.set(lines_);
    view_model_->logScroll = kScrollToEnd;
}

void BuildPanel::show_summary(std::string text) {
    view_model_->summary = std::move(text);
}

const std::string& BuildPanel::first_error() const {
    return first_error_;
}

}
