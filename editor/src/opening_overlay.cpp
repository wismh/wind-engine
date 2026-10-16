#include "opening_overlay.h"

#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace editor {
namespace {

std::string_view trim_view(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(kSpace);
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(kSpace) - begin + 1);
}

bool parse_uint(std::string_view text, std::size_t& pos, std::uint32_t& out) {
    while (pos < text.size() && text[pos] == ' ') {
        ++pos;
    }
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }
    std::uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        value = value * 10 + static_cast<std::uint64_t>(text[pos] - '0');
        if (value > 1'000'000u) {
            return false;
        }
        ++pos;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

// Parses Ninja `[N/M]` or CMake Makefile `[ N%]` / `N%` progress ratios in [0, 1].
std::optional<float> parse_build_ratio(std::string_view line) {
    for (std::size_t bracket = line.find('['); bracket != std::string_view::npos;
            bracket = line.find('[', bracket + 1)) {
        std::size_t pos = bracket + 1;
        std::uint32_t first = 0;
        if (!parse_uint(line, pos, first)) {
            continue;
        }
        if (pos < line.size() && line[pos] == '/') {
            ++pos;
            std::uint32_t total = 0;
            if (!parse_uint(line, pos, total) || total == 0 || first > total) {
                continue;
            }
            while (pos < line.size() && line[pos] == ' ') {
                ++pos;
            }
            if (pos < line.size() && line[pos] == ']') {
                return static_cast<float>(first) / static_cast<float>(total);
            }
        } else if (pos < line.size() && line[pos] == '%') {
            ++pos;
            while (pos < line.size() && line[pos] == ' ') {
                ++pos;
            }
            if (pos < line.size() && line[pos] == ']' && first <= 100) {
                return static_cast<float>(first) / 100.0f;
            }
        }
    }
    for (std::size_t pct = line.find('%'); pct != std::string_view::npos; pct = line.find('%', pct + 1)) {
        if (pct == 0 || !std::isdigit(static_cast<unsigned char>(line[pct - 1]))) {
            continue;
        }
        std::size_t start = pct;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(line[start - 1]))) {
            --start;
        }
        std::size_t pos = start;
        std::uint32_t percent = 0;
        if (parse_uint(line, pos, percent) && pos == pct && percent <= 100) {
            return static_cast<float>(percent) / 100.0f;
        }
    }
    return std::nullopt;
}

}

OpeningOverlay::OpeningOverlay()
    : view_model_(std::make_shared<OpeningViewModel>()) {
    view_model_->skip.bind_to<OpeningOverlay, &OpeningOverlay::skip>(*this);
}

const std::shared_ptr<OpeningViewModel>& OpeningOverlay::view_model() const {
    return view_model_;
}

void OpeningOverlay::spawn(engine::ecs::World& world, engine::WindowId window) {
    world_ = &world;
    canvas_ = world.create();
    world.emplace<engine::ui::UiCanvas>(canvas_, engine::ui::UiCanvas{
            .document = assets::ui::opening,
            .stylesheet = assets::css::editor,
            .data_context = view_model_,
            .fit = open_ ? engine::ui::UiFit::FillWindow : engine::ui::UiFit::Fixed,
            .order = kCanvasOrder,
            .window = window,
    });
}

void OpeningOverlay::open(std::string project_name, std::string target) {
    open_ = true;
    skip_requested_ = false;
    target_ = std::move(target);
    view_model_->title = "Opening " + project_name + "...";
    view_model_->stepText = "Building " + target_ + "...";
    view_model_->detailText = std::string("Starting CMake...");
    set_progress(0.10f);
    sync_canvas();
}

void OpeningOverlay::close() {
    open_ = false;
    skip_requested_ = false;
    sync_canvas();
}

bool OpeningOverlay::is_open() const noexcept {
    return open_;
}

void OpeningOverlay::feed_lines(std::span<const std::string> lines) {
    for (const std::string& raw : lines) {
        const std::string_view line = trim_view(raw);
        if (line.empty()) {
            continue;
        }
        view_model_->detailText = std::string(line);
        if (line.starts_with("> cmake -S")) {
            view_model_->stepText = std::string("Configuring CMake...");
            set_progress(std::max(progress_, 0.15f));
        } else if (line.starts_with("> cmake --build")) {
            view_model_->stepText = "Building " + target_ + "...";
            set_progress(std::max(progress_, 0.35f));
        } else if (const std::optional<float> ratio = parse_build_ratio(line)) {
            const float mapped = std::clamp(0.35f + 0.60f * (*ratio), 0.35f, 0.95f);
            set_progress(std::max(progress_, mapped));
        } else if (progress_ < 0.90f) {
            set_progress(std::min(0.90f, progress_ + 0.02f));
        }
    }
}

void OpeningOverlay::skip() {
    if (open_) {
        skip_requested_ = true;
    }
}

bool OpeningOverlay::take_skip_requested() {
    return std::exchange(skip_requested_, false);
}

float OpeningOverlay::progress() const noexcept {
    return progress_;
}

engine::ecs::Entity OpeningOverlay::canvas() const noexcept {
    return canvas_;
}

void OpeningOverlay::set_progress(float value) {
    progress_ = std::clamp(value, 0.0f, 1.0f);
    view_model_->progressWidth = progress_ * kTrackWidth;
}

void OpeningOverlay::sync_canvas() {
    if (world_ == nullptr || canvas_ == engine::ecs::Entity{}) {
        return;
    }
    engine::ui::UiCanvas& canvas = world_->get<engine::ui::UiCanvas>(canvas_);
    if (open_) {
        canvas.fit = engine::ui::UiFit::FillWindow;
    } else {
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {};
    }
}

}
