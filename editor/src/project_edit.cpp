#include "project_edit.h"

#include <engine/resources/asset_guid.h>
#include <engine/resources/meta.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace editor {
namespace {

constexpr char kOccupied[] = "Something already has that name.";
constexpr char kOutside[] = "That path is outside the project.";
constexpr char kIntoSelf[] = "Can't move a folder into itself.";
constexpr char kProjectFile[] = "wind_project.toml stays where it is.";
constexpr char kBadName[] = "That name isn't allowed.";
constexpr char kMissing[] = "That file isn't there anymore.";
constexpr char kFailed[] = "Couldn't change the project files.";
constexpr char kChanged[] = "Couldn't undo: the file changed after it was created.";
constexpr char kWorldBytes[] = "version = 1\n";
constexpr char kBuildNote[] = ". The build will not compile it until CMakeLists.txt lists it.";
constexpr char kLink[] = "Can't delete a link.";

std::filesystem::path key_path(std::string_view key) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(key.data()), key.size()));
}

std::string path_key(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// `key` resolved under `root`, or nullopt when it would leave the project or names `..`.
std::optional<std::filesystem::path> under_root(const std::filesystem::path& root, std::string_view key) {
    if (key.empty() || key.starts_with('/') || key.starts_with('\\') || key.find(':') != std::string_view::npos ||
            key.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }
    const std::filesystem::path relative = key_path(key);
    if (relative.is_absolute()) {
        return std::nullopt;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    const std::filesystem::path full = (root / relative).lexically_normal();
    const std::filesystem::path back = full.lexically_relative(root.lexically_normal());
    if (back.empty() || *back.begin() == "..") {
        return std::nullopt;
    }
    return full;
}

bool exists_path(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool path_is_directory(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

bool path_is_symlink(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_symlink(path, error);
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

std::optional<std::string> read_guid(const std::filesystem::path& meta) {
    const std::optional<std::string> text = read_text(meta);
    if (!text) {
        return std::nullopt;
    }
    const auto parsed = engine::parse_asset_meta(*text);
    if (!parsed) {
        return std::nullopt;
    }
    return std::string(parsed->guid.hex());
}

bool same_file(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    return std::filesystem::equivalent(left, right, error);
}

// Rename, including a case-only change on a case-insensitive volume (direct rename then sees the destination).
bool rename_path(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (!error) {
        return true;
    }
    if (!exists_path(from) || !exists_path(to) || !same_file(from, to)) {
        return false;
    }
    std::filesystem::path temp = to;
    temp += ".__wind_rename";
    std::filesystem::rename(from, temp, error);
    if (error) {
        return false;
    }
    std::filesystem::rename(temp, to, error);
    if (error) {
        std::error_code back;
        std::filesystem::rename(temp, from, back);
        return false;
    }
    return true;
}

bool relocate_pair(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::path from_meta = from;
    from_meta += ".meta";
    std::filesystem::path to_meta = to;
    to_meta += ".meta";
    const bool meta = exists_path(from_meta);
    if (!rename_path(from, to)) {
        return false;
    }
    if (meta && !rename_path(from_meta, to_meta)) {
        std::error_code error;
        std::filesystem::rename(to, from, error);
        return false;
    }
    return true;
}

bool copy_tree(const std::filesystem::path& from, const std::filesystem::path& to, std::string& notice) {
    std::error_code error;
    if (path_is_symlink(from)) {
        if (path_is_directory(from)) {
            notice = "Can't copy a linked folder.";
            return false;
        }
        std::filesystem::copy_symlink(from, to, error);
        if (error) {
            notice = kFailed;
            return false;
        }
        return true;
    }
    if (path_is_directory(from)) {
        std::filesystem::create_directory(to, error);
        if (error) {
            notice = kFailed;
            return false;
        }
        for (std::filesystem::directory_iterator it(from, error), end; it != end; it.increment(error)) {
            if (error) {
                notice = kFailed;
                return false;
            }
            if (it->path().extension() == ".meta") {
                continue;
            }
            if (!copy_tree(it->path(), to / it->path().filename(), notice)) {
                return false;
            }
        }
        return true;
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error);
    if (error) {
        notice = kFailed;
        return false;
    }
    return true;
}

bool remove_tree(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    return !error;
}

std::string_view create_bytes(ProjectCreateKind kind) {
    switch (kind) {
        case ProjectCreateKind::World:
            return kWorldBytes;
        case ProjectCreateKind::Header:
        case ProjectCreateKind::Source:
        case ProjectCreateKind::Directory:
            return {};
    }
    return {};
}

bool inside_key(std::string_view path, std::string_view directory) {
    if (directory.empty()) {
        return false;
    }
    if (path == directory) {
        return true;
    }
    return path.size() > directory.size() && path.starts_with(directory) && path[directory.size()] == '/';
}

} // namespace

bool project_name_ok(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.back() == '.' || name.back() == ' ') {
        return false;
    }
    constexpr std::string_view kForbidden = "<>:\"|?*";
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 32 || c == '/' || c == '\\' ||
                kForbidden.find(c) != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

std::string project_parent_key(std::string_view key) {
    const std::size_t slash = key.rfind('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    return std::string(key.substr(0, slash));
}

std::string project_leaf_name(std::string_view key) {
    const std::size_t slash = key.rfind('/');
    return std::string(slash == std::string_view::npos ? key : key.substr(slash + 1));
}

std::string project_join_key(std::string_view parent, std::string_view name) {
    if (parent.empty()) {
        return std::string(name);
    }
    std::string key(parent);
    key.push_back('/');
    key.append(name);
    return key;
}

std::string project_unique_name(const std::filesystem::path& directory, std::string_view stem,
        std::string_view extension) {
    for (int index = 1; index < 10000; ++index) {
        std::string name(stem);
        if (index > 1) {
            name.push_back(' ');
            name += std::to_string(index);
        }
        name.append(extension);
        if (!exists_path(directory / key_path(name))) {
            return name;
        }
    }
    return {};
}

ProjectEditOutcome ProjectStep::refuse(std::string notice) {
    ProjectEditOutcome outcome;
    outcome.result.notice = std::move(notice);
    return outcome;
}

ProjectEditOutcome ProjectStep::finish(ProjectStepListener& listener, const std::filesystem::path& root, Edit edit,
        std::string notice) {
    ProjectEditOutcome outcome;
    outcome.result = result_of(edit, true);
    outcome.result.notice = std::move(notice);
    edit.applied = true;
    outcome.step.reset(new ProjectStep(listener, root, std::move(edit)));
    return outcome;
}

ProjectEditResult ProjectStep::result_of(const Edit& edit, bool forward) {
    ProjectEditResult result;
    result.ok = true;
    if (forward) {
        if (edit.kind == Edit::Kind::Delete) {
            result.select = project_parent_key(edit.from);
            result.removed.push_back(edit.from);
        } else {
            result.select = edit.to;
            if (edit.kind == Edit::Kind::Relocate) {
                result.moved.push_back(ProjectRelocation{edit.from, edit.to});
            }
        }
        return result;
    }
    if (edit.kind == Edit::Kind::Relocate) {
        result.select = edit.from;
        result.moved.push_back(ProjectRelocation{edit.to, edit.from});
    } else if (edit.kind == Edit::Kind::Copy || edit.kind == Edit::Kind::Delete) {
        result.select = edit.from;
    } else {
        result.select = project_parent_key(edit.to);
    }
    return result;
}

ProjectStep::ProjectStep(ProjectStepListener& listener, std::filesystem::path root, Edit edit)
    : listener_(&listener), root_(std::move(root)), edit_(std::move(edit)) {}

ProjectEditOutcome ProjectStep::relocate(ProjectStepListener& listener,
        const std::filesystem::path& root, std::string from, std::string to) {
    if (from == to) {
        ProjectEditOutcome same;
        same.result.ok = true;
        same.result.select = std::move(to);
        return same;
    }
    if (from == "wind_project.toml" || to == "wind_project.toml") {
        return refuse(kProjectFile);
    }
    if (!project_name_ok(project_leaf_name(to))) {
        return refuse(kBadName);
    }
    if (inside_key(to, from)) {
        return refuse(kIntoSelf);
    }
    const std::optional<std::filesystem::path> from_path = under_root(root, from);
    const std::optional<std::filesystem::path> to_path = under_root(root, to);
    if (!from_path || !to_path) {
        return refuse(kOutside);
    }
    if (!exists_path(*from_path)) {
        return refuse(kMissing);
    }
    if (exists_path(*to_path) && !same_file(*from_path, *to_path)) {
        return refuse(kOccupied);
    }
    if (!exists_path(to_path->parent_path())) {
        return refuse(kMissing);
    }
    Edit edit;
    edit.kind = Edit::Kind::Relocate;
    edit.from = std::move(from);
    edit.to = std::move(to);
    std::string notice;
    if (!apply(edit, root, notice)) {
        return refuse(notice.empty() ? std::string(kFailed) : std::move(notice));
    }
    return finish(listener, root, std::move(edit), {});
}

ProjectEditOutcome ProjectStep::copy_entry(ProjectStepListener& listener,
        const std::filesystem::path& root, std::string from, std::string to) {
    if (!project_name_ok(project_leaf_name(to))) {
        return refuse(kBadName);
    }
    if (inside_key(to, from)) {
        return refuse(kIntoSelf);
    }
    const std::optional<std::filesystem::path> from_path = under_root(root, from);
    const std::optional<std::filesystem::path> to_path = under_root(root, to);
    if (!from_path || !to_path) {
        return refuse(kOutside);
    }
    if (!exists_path(*from_path) || exists_path(*to_path) || !exists_path(to_path->parent_path())) {
        return refuse(exists_path(*to_path) ? std::string(kOccupied) : std::string(kMissing));
    }
    Edit edit;
    edit.kind = Edit::Kind::Copy;
    edit.from = std::move(from);
    edit.to = std::move(to);
    std::string notice;
    if (!apply(edit, root, notice)) {
        return refuse(notice.empty() ? std::string(kFailed) : std::move(notice));
    }
    return finish(listener, root, std::move(edit), {});
}

ProjectEditOutcome ProjectStep::create(ProjectStepListener& listener,
        const std::filesystem::path& root, std::string key, ProjectCreateKind kind) {
    if (!project_name_ok(project_leaf_name(key)) || key == "wind_project.toml") {
        return refuse(key == "wind_project.toml" ? std::string(kProjectFile) : std::string(kBadName));
    }
    const std::optional<std::filesystem::path> path = under_root(root, key);
    if (!path) {
        return refuse(kOutside);
    }
    if (exists_path(*path) || !exists_path(path->parent_path())) {
        return refuse(exists_path(*path) ? std::string(kOccupied) : std::string(kMissing));
    }
    Edit edit;
    edit.kind = kind == ProjectCreateKind::Directory ? Edit::Kind::CreateDir : Edit::Kind::CreateFile;
    edit.to = std::move(key);
    edit.bytes = std::string(create_bytes(kind));
    std::string notice;
    if (!apply(edit, root, notice)) {
        return refuse(notice.empty() ? std::string(kFailed) : std::move(notice));
    }
    std::string built;
    if (kind == ProjectCreateKind::Header || kind == ProjectCreateKind::Source) {
        built = "Created " + edit.to + kBuildNote;
    }
    return finish(listener, root, std::move(edit), std::move(built));
}

ProjectEditOutcome ProjectStep::remove(ProjectStepListener& listener,
        const std::filesystem::path& root, std::string key) {
    if (key == "wind_project.toml") {
        return refuse(kProjectFile);
    }
    if (!project_name_ok(project_leaf_name(key))) {
        return refuse(kBadName);
    }
    const std::optional<std::filesystem::path> path = under_root(root, key);
    if (!path) {
        return refuse(kOutside);
    }
    if (!exists_path(*path)) {
        return refuse(kMissing);
    }
    Edit edit;
    edit.kind = Edit::Kind::Delete;
    edit.from = std::move(key);
    std::string notice;
    if (!apply(edit, root, notice)) {
        return refuse(notice.empty() ? std::string(kFailed) : std::move(notice));
    }
    return finish(listener, root, std::move(edit), {});
}

bool ProjectStep::undo() {
    std::string notice;
    if (!revert(edit_, root_, notice)) {
        ProjectEditResult failed;
        failed.notice = notice.empty() ? std::string(kFailed) : std::move(notice);
        listener_->project_step_ran(failed);
        return false;
    }
    edit_.applied = false;
    listener_->project_step_ran(result_of(edit_, false));
    return true;
}

bool ProjectStep::redo() {
    std::string notice;
    if (!apply(edit_, root_, notice)) {
        ProjectEditResult failed;
        failed.notice = notice.empty() ? std::string(kFailed) : std::move(notice);
        listener_->project_step_ran(failed);
        return false;
    }
    edit_.applied = true;
    listener_->project_step_ran(result_of(edit_, true));
    return true;
}

bool ProjectStep::valid() const {
    return matches_one(edit_, root_);
}

bool ProjectStep::apply(Edit& edit, const std::filesystem::path& root, std::string& notice) {
    if (edit.kind == Edit::Kind::Delete) {
        const std::optional<std::filesystem::path> from = under_root(root, edit.from);
        if (!from) {
            notice = kOutside;
            return false;
        }
        if (!exists_path(*from)) {
            notice = kMissing;
            return false;
        }
        if (path_is_symlink(*from)) {
            notice = kLink;
            return false;
        }
        edit.bytes.clear();
        edit.files.clear();
        edit.metas.clear();
        const bool directory = path_is_directory(*from);
        if (directory) {
            if (!capture_tree(*from, {}, edit.files, notice)) {
                return false;
            }
        } else {
            const std::optional<std::string> text = read_text(*from);
            if (!text) {
                notice = kFailed;
                return false;
            }
            edit.bytes = *text;
        }
        collect_metas(*from, directory, edit.metas);
        if (!remove_tree(*from)) {
            notice = kFailed;
            return false;
        }
        std::error_code error;
        for (const MetaExpect& expect : edit.metas) {
            if (expect.sibling) {
                std::filesystem::remove(meta_file(*from, expect), error);
            }
        }
        edit.applied = true;
        return true;
    }
    const std::optional<std::filesystem::path> to = under_root(root, edit.to);
    if (!to) {
        notice = kOutside;
        return false;
    }
    if (edit.kind == Edit::Kind::CreateDir) {
        std::error_code error;
        if (!std::filesystem::create_directory(*to, error) || error) {
            notice = kFailed;
            return false;
        }
        edit.applied = true;
        return true;
    }
    if (edit.kind == Edit::Kind::CreateFile) {
        if (!write_text(*to, edit.bytes)) {
            notice = kFailed;
            return false;
        }
        if (edit.to.starts_with("assets/") || edit.to == "assets") {
            (void) engine::write_missing_metas(root / "assets");
        }
        collect_metas(*to, false, edit.metas);
        edit.applied = true;
        return true;
    }
    const std::optional<std::filesystem::path> from = under_root(root, edit.from);
    if (!from || !exists_path(*from)) {
        notice = kMissing;
        return false;
    }
    if (edit.kind == Edit::Kind::Relocate) {
        if (!relocate_pair(*from, *to)) {
            notice = kFailed;
            return false;
        }
        collect_metas(*to, path_is_directory(*to), edit.metas);
        edit.applied = true;
        return true;
    }
    if (!copy_tree(*from, *to, notice)) {
        (void) remove_tree(*to);
        return false;
    }
    if (edit.to.starts_with("assets/") || project_parent_key(edit.to).starts_with("assets")) {
        (void) engine::write_missing_metas(root / "assets");
    }
    collect_metas(*to, path_is_directory(*to), edit.metas);
    edit.applied = true;
    return true;
}

std::filesystem::path ProjectStep::meta_file(const std::filesystem::path& asset, const MetaExpect& expect) {
    if (expect.sibling) {
        std::filesystem::path path = asset;
        path += ".meta";
        return path;
    }
    return asset / key_path(expect.relative);
}

void ProjectStep::collect_metas(const std::filesystem::path& asset, bool directory, std::vector<MetaExpect>& out) {
    out.clear();
    std::filesystem::path sibling = asset;
    sibling += ".meta";
    if (exists_path(sibling)) {
        if (const std::optional<std::string> text = read_text(sibling)) {
            std::string guid;
            if (const auto parsed = engine::parse_asset_meta(*text)) {
                guid = std::string(parsed->guid.hex());
            }
            out.push_back(MetaExpect{.sibling = true, .guid = std::move(guid), .text = *text});
        }
    }
    if (!directory) {
        return;
    }
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(asset, options, error), end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        if (it->path().extension() != ".meta" || !it->is_regular_file(error)) {
            continue;
        }
        const std::optional<std::string> text = read_text(it->path());
        if (!text) {
            continue;
        }
        std::string guid;
        if (const auto parsed = engine::parse_asset_meta(*text)) {
            guid = std::string(parsed->guid.hex());
        }
        out.push_back(MetaExpect{
                .sibling = false,
                .relative = path_key(it->path().lexically_relative(asset)),
                .guid = std::move(guid),
                .text = *text,
        });
    }
}

bool ProjectStep::metas_match(const std::filesystem::path& asset, const std::vector<MetaExpect>& metas) {
    for (const MetaExpect& expect : metas) {
        const std::filesystem::path path = meta_file(asset, expect);
        if (expect.guid.empty()) {
            const std::optional<std::string> text = read_text(path);
            if (!text || *text != expect.text) {
                return false;
            }
            continue;
        }
        const std::optional<std::string> guid = read_guid(path);
        if (!guid || *guid != expect.guid) {
            return false;
        }
    }
    return true;
}

bool ProjectStep::capture_tree(const std::filesystem::path& path, const std::string& relative,
        std::vector<StoredFile>& out, std::string& notice) {
    if (path_is_symlink(path)) {
        notice = kLink;
        return false;
    }
    if (path_is_directory(path)) {
        out.push_back(StoredFile{relative, true, {}});
        std::error_code error;
        for (std::filesystem::directory_iterator it(path, error), end; it != end; it.increment(error)) {
            if (error) {
                notice = kFailed;
                return false;
            }
            if (it->path().extension() == ".meta") {
                continue;
            }
            const std::string name = path_key(it->path().filename());
            const std::string child = relative.empty() ? name : relative + "/" + name;
            if (!capture_tree(it->path(), child, out, notice)) {
                return false;
            }
        }
        return true;
    }
    const std::optional<std::string> text = read_text(path);
    if (!text) {
        notice = kFailed;
        return false;
    }
    out.push_back(StoredFile{relative, false, *text});
    return true;
}

bool ProjectStep::restore_deleted(const std::filesystem::path& asset, const Edit& edit, std::string& notice) {
    if (edit.files.empty()) {
        if (!write_text(asset, edit.bytes)) {
            notice = kFailed;
            return false;
        }
    }
    for (const StoredFile& node : edit.files) {
        const std::filesystem::path dest = node.relative.empty() ? asset : asset / key_path(node.relative);
        std::error_code error;
        if (node.directory) {
            std::filesystem::create_directories(dest, error);
            if (error) {
                notice = kFailed;
                return false;
            }
            continue;
        }
        std::filesystem::create_directories(dest.parent_path(), error);
        if (error || !write_text(dest, node.bytes)) {
            notice = kFailed;
            return false;
        }
    }
    for (const MetaExpect& expect : edit.metas) {
        if (expect.text.empty()) {
            continue;
        }
        const std::filesystem::path meta = meta_file(asset, expect);
        std::error_code error;
        std::filesystem::create_directories(meta.parent_path(), error);
        if (error || !write_text(meta, expect.text)) {
            notice = kFailed;
            return false;
        }
    }
    return true;
}

bool ProjectStep::revert(Edit& edit, const std::filesystem::path& root, std::string& notice) {
    if (edit.kind == Edit::Kind::Delete) {
        const std::optional<std::filesystem::path> from = under_root(root, edit.from);
        if (!from) {
            notice = kOutside;
            return false;
        }
        if (exists_path(*from)) {
            notice = kOccupied;
            return false;
        }
        if (!restore_deleted(*from, edit, notice)) {
            (void) remove_tree(*from);
            std::error_code error;
            for (const MetaExpect& expect : edit.metas) {
                if (expect.sibling) {
                    std::filesystem::remove(meta_file(*from, expect), error);
                }
            }
            return false;
        }
        edit.applied = false;
        return true;
    }
    const std::optional<std::filesystem::path> to = under_root(root, edit.to);
    if (!to) {
        notice = kOutside;
        return false;
    }
    if (edit.kind == Edit::Kind::CreateDir) {
        std::error_code error;
        const std::filesystem::directory_iterator it(*to, error);
        if (error || it != std::filesystem::directory_iterator()) {
            notice = kChanged;
            return false;
        }
        if (!std::filesystem::remove(*to, error) || error) {
            notice = kFailed;
            return false;
        }
        edit.applied = false;
        return true;
    }
    if (edit.kind == Edit::Kind::CreateFile) {
        const std::optional<std::string> text = read_text(*to);
        if (!text || *text != edit.bytes || !metas_match(*to, edit.metas)) {
            notice = kChanged;
            return false;
        }
        std::error_code error;
        std::filesystem::remove(*to, error);
        if (error) {
            notice = kFailed;
            return false;
        }
        for (const MetaExpect& expect : edit.metas) {
            std::filesystem::remove(meta_file(*to, expect), error);
        }
        edit.applied = false;
        return true;
    }
    if (edit.kind == Edit::Kind::Copy) {
        if (!remove_tree(*to)) {
            notice = kFailed;
            return false;
        }
        std::error_code error;
        for (const MetaExpect& expect : edit.metas) {
            std::filesystem::remove(meta_file(*to, expect), error);
        }
        edit.applied = false;
        return true;
    }
    const std::optional<std::filesystem::path> from = under_root(root, edit.from);
    if (!from) {
        notice = kOutside;
        return false;
    }
    if (!relocate_pair(*to, *from)) {
        notice = kFailed;
        return false;
    }
    edit.applied = false;
    return true;
}

bool ProjectStep::matches_one(const Edit& edit, const std::filesystem::path& root) {
    if (edit.kind == Edit::Kind::Delete) {
        const std::optional<std::filesystem::path> path = under_root(root, edit.from);
        if (!path) {
            return false;
        }
        if (edit.applied) {
            if (exists_path(*path)) {
                return false;
            }
            for (const MetaExpect& expect : edit.metas) {
                if (exists_path(meta_file(*path, expect))) {
                    return false;
                }
            }
            return true;
        }
        if (!exists_path(*path)) {
            return false;
        }
        if (edit.files.empty()) {
            const std::optional<std::string> text = read_text(*path);
            if (!text || *text != edit.bytes) {
                return false;
            }
        } else {
            for (const StoredFile& node : edit.files) {
                const std::filesystem::path dest = node.relative.empty() ? *path : *path / key_path(node.relative);
                if (node.directory) {
                    if (!path_is_directory(dest)) {
                        return false;
                    }
                    continue;
                }
                const std::optional<std::string> text = read_text(dest);
                if (!text || *text != node.bytes) {
                    return false;
                }
            }
        }
        return metas_match(*path, edit.metas);
    }
    const std::string& key = edit.applied ? edit.to : edit.from;
    if (edit.kind != Edit::Kind::Relocate && !edit.applied) {
        const std::optional<std::filesystem::path> created = under_root(root, edit.to);
        return created && !exists_path(*created);
    }
    if (key.empty()) {
        return false;
    }
    const std::optional<std::filesystem::path> path = under_root(root, key);
    const std::optional<std::filesystem::path> other = under_root(root, edit.applied ? edit.from : edit.to);
    if (!path || !exists_path(*path)) {
        return false;
    }
    if (edit.kind == Edit::Kind::Relocate && other && exists_path(*other) && !same_file(*path, *other)) {
        return false;
    }
    if (edit.kind == Edit::Kind::CreateFile && edit.applied) {
        const std::optional<std::string> text = read_text(*path);
        if (!text || *text != edit.bytes) {
            return false;
        }
    }
    if (edit.kind == Edit::Kind::CreateDir) {
        return edit.applied ? path_is_directory(*path) : !exists_path(*path);
    }
    return metas_match(*path, edit.metas);
}

} // namespace editor
