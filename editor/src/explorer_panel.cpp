#include "explorer_panel.h"

#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace editor {
namespace {

constexpr char kNoProject[] = "No project open.";
constexpr char kNothingSelected[] = "Select a file or folder.";

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// flatten_tree over a scan: a node is an entry of it.
struct EntrySource {
    [[nodiscard]] const std::string& key(const ProjectEntry* entry) const { return entry->key; }
    [[nodiscard]] bool has_children(const ProjectEntry* entry) const { return !entry->children.empty(); }
    template<typename Visit>
    void for_each_child(const ProjectEntry* entry, Visit&& visit) const {
        for (const ProjectEntry& child : entry->children) {
            visit(&child);
        }
    }
};

const ProjectEntry* find_entry(const std::vector<ProjectEntry>& roots, std::string_view key) {
    const std::vector<ProjectEntry>* level = &roots;
    const ProjectEntry* found = nullptr;
    while (!key.empty()) {
        const std::size_t slash = key.find('/');
        const std::string_view name = key.substr(0, slash);
        found = nullptr;
        for (const ProjectEntry& entry : *level) {
            if (entry.name == name) {
                found = &entry;
                break;
            }
        }
        if (found == nullptr) {
            return nullptr;
        }
        level = &found->children;
        key = slash == std::string_view::npos ? std::string_view{} : key.substr(slash + 1);
    }
    return found;
}

std::string size_text(std::uintmax_t bytes) {
    if (bytes < 1024) {
        return std::format("{} bytes", bytes);
    }
    constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    return std::format("{:.1f} {}", value, kUnits[unit]);
}

std::string detail_text(const ProjectEntry& entry) {
    if (!entry.directory) {
        return entry.key + "\nFile, " + size_text(entry.size);
    }
    const std::size_t items = entry.children.size();
    return entry.key + "\nFolder, " + std::to_string(items) + (items == 1 ? " item" : " items");
}

}

ExplorerPanel::ExplorerPanel() : view_model_(std::make_shared<ExplorerViewModel>()) {
    view_model_->refresh.bind_to<ExplorerPanel, &ExplorerPanel::rescan, &ExplorerPanel::can_rescan>(*this);
    view_model_->rootText = std::string(kNoProject);
}

const std::shared_ptr<ExplorerViewModel>& ExplorerPanel::view_model() const {
    return view_model_;
}

void ExplorerPanel::open(const std::filesystem::path& directory) {
    directory_ = directory;
    expansion_.reset();
    selected_.clear();
    rows_.clear();
    rescan();
}

void ExplorerPanel::close() {
    directory_.clear();
    scan_ = {};
    expansion_.reset();
    selected_.clear();
    rows_.clear();
    view_model_->rows.set({});
    view_model_->detail = std::string();
    view_model_->rootText = std::string(kNoProject);
}

void ExplorerPanel::rescan() {
    if (directory_.empty()) {
        return;
    }
    scan_ = scan_project(directory_);
    expansion_.retain([this](const std::string& key) { return find_entry(scan_.roots, key) != nullptr; });
    if (find_entry(scan_.roots, selected_) == nullptr) {
        selected_.clear();
    }
    std::string root = path_text(directory_);
    if (scan_.truncated) {
        root += std::format("  (the first {} entries)", kMaxProjectEntries);
    }
    view_model_->rootText = std::move(root);
    show_rows();
    show_selection();
}

bool ExplorerPanel::can_rescan() const {
    return !directory_.empty();
}

void ExplorerPanel::select(const std::string& key) {
    if (find_entry(scan_.roots, key) == nullptr) {
        return;
    }
    selected_ = key;
    show_rows();
    show_selection();
}

void ExplorerPanel::toggle(const std::string& key) {
    const ProjectEntry* entry = find_entry(scan_.roots, key);
    if (entry == nullptr || entry->children.empty()) {
        return;
    }
    expansion_.toggle(key);
    show_rows();
}

std::optional<std::size_t> ExplorerPanel::navigate(engine::ui::TreeNav nav) {
    const std::vector<std::shared_ptr<ExplorerRowViewModel>>& rows = view_model_->rows.get();
    std::vector<engine::ui::TreeRowInfo> infos;
    infos.reserve(rows.size());
    std::size_t current = engine::ui::kNoTreeRow;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        infos.push_back(rows[i]->tree());
        if (rows[i]->key() == selected_) {
            current = i;
        }
    }
    const engine::ui::TreeNavResult result = engine::ui::tree_navigate(infos, current, nav);
    if (result.row == engine::ui::kNoTreeRow) {
        return std::nullopt;
    }
    // Copied: toggle and select replace the rows.
    const std::string target = rows[result.row]->key();
    if (result.toggle) {
        toggle(target);
    }
    if (result.row != current) {
        select(target);
    }
    return result.row;
}

const std::string& ExplorerPanel::selected() const {
    return selected_;
}

void ExplorerPanel::show_rows() {
    std::vector<const ProjectEntry*> roots;
    roots.reserve(scan_.roots.size());
    for (const ProjectEntry& entry : scan_.roots) {
        roots.push_back(&entry);
    }
    std::vector<std::shared_ptr<ExplorerRowViewModel>> visible;
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> kept;
    (void) engine::ui::flatten_tree(roots, expansion_, EntrySource{},
            [&](const ProjectEntry* entry, const engine::ui::TreeRowInfo& info) {
                std::shared_ptr<ExplorerRowViewModel> slot;
                if (const auto it = rows_.find(entry->key); it != rows_.end()) {
                    slot = it->second;
                } else {
                    slot = std::make_shared<ExplorerRowViewModel>(*this);
                }
                slot->show(*entry, info, entry->key == selected_);
                visible.push_back(slot);
                kept.emplace(entry->key, std::move(slot));
            });
    rows_ = std::move(kept);
    view_model_->rows.set(std::move(visible));
}

void ExplorerPanel::show_selection() {
    const ProjectEntry* entry = selected_.empty() ? nullptr : find_entry(scan_.roots, selected_);
    view_model_->detail = entry != nullptr ? detail_text(*entry) : std::string(kNothingSelected);
}

}
