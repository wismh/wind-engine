#include "dock_layout_file.h"

#include <engine/log.h>

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}

DockLayoutFile::DockLayoutFile(std::filesystem::path path) : path_(std::move(path)) {}

const std::filesystem::path& DockLayoutFile::path() const {
    return path_;
}

std::optional<engine::ui::DockLayout> DockLayoutFile::load() const {
    if (path_.empty()) {
        return std::nullopt;
    }
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::optional<engine::ui::DockLayout> layout = engine::ui::dock_layout_from_text(text);
    if (!layout) {
        engine::log::warn("Editor: " + path_text(path_) + " is not a panel layout; using the default");
    }
    return layout;
}

bool DockLayoutFile::save(const engine::ui::DockLayout& layout) const {
    if (path_.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    std::filesystem::path temp = path_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << engine::ui::dock_layout_to_text(layout);
        if (!out.flush()) {
            engine::log::warn("Editor: cannot write " + path_text(temp));
            return false;
        }
    }
    std::filesystem::rename(temp, path_, error);
    if (error) {
        engine::log::warn("Editor: cannot replace " + path_text(path_) + ": " + error.message());
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

}
