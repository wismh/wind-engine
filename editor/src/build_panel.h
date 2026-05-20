#pragma once

#include "build_line_view_model.h"
#include "build_view_model.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// How a build log line reads: an error or a warning of CMake, MSBuild, the compiler, or the linker.
enum class LineTone {
    Plain,
    Warning,
    Error,
};

[[nodiscard]] LineTone tone_of(std::string_view line);

// The Build tab: the output of the last build and a summary line above it. Lines are plain copies; the panel
// keeps the last kMaxLines of them.
class BuildPanel {
public:
    static constexpr std::size_t kMaxLines = 5000;

    BuildPanel();

    BuildPanel(const BuildPanel&) = delete;
    BuildPanel& operator=(const BuildPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<BuildViewModel>& view_model() const;

    // A new build: drops the old log.
    void clear();
    // Adds lines at the end and scrolls to them.
    void append(std::vector<std::string> lines);
    void show_summary(std::string text);

    // The first error line of this log, or empty.
    [[nodiscard]] const std::string& first_error() const;

private:
    std::shared_ptr<BuildViewModel> view_model_;
    std::vector<std::shared_ptr<BuildLineViewModel>> lines_;
    std::string first_error_;
};

}
