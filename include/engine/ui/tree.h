#pragma once

// docs/tech/modules/UI.md#trees

#include <engine/core/key_code.h>

#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ui {

    // No row: a root's parent, or no selection.
    inline constexpr std::size_t kNoTreeRow = static_cast<std::size_t>(-1);

    // One visible row of a flattened tree. Rows are depth first, so a node's children are the rows right
    // after it with a greater depth.
    struct TreeRowInfo {
        int depth = 0;
        bool has_children = false;
        // False for a leaf.
        bool expanded = false;
        // Row index of the parent, or kNoTreeRow for a root.
        std::size_t parent = kNoTreeRow;

        bool operator==(const TreeRowInfo &) const = default;
    };

    // Which nodes of a tree are expanded, by a key that stays the same across rebuilds. Keeps only the
    // keys whose state differs from the default.
    template<typename Key, typename Hash = std::hash<Key>>
    class TreeExpansion {
    public:
        TreeExpansion() = default;
        explicit TreeExpansion(bool expanded_by_default) : expanded_by_default_(expanded_by_default) {}

        [[nodiscard]] bool expanded_by_default() const { return expanded_by_default_; }

        [[nodiscard]] bool is_expanded(const Key &key) const {
            return flipped_.contains(key) != expanded_by_default_;
        }

        void set_expanded(const Key &key, bool expanded) {
            if (expanded == expanded_by_default_) {
                flipped_.erase(key);
            } else {
                flipped_.insert(key);
            }
        }

        void toggle(const Key &key) { set_expanded(key, !is_expanded(key)); }

        // Forgets every key `keep` rejects, so a removed node does not pin its state forever.
        template<typename Keep>
        void retain(Keep &&keep) {
            std::erase_if(flipped_, [&keep](const Key &key) { return !keep(key); });
        }

        // Every node back to the default.
        void reset() { flipped_.clear(); }

    private:
        bool expanded_by_default_ = true;
        std::unordered_set<Key, Hash> flipped_;
    };

    // How flatten_tree reads a caller's tree. `Node` is a value the source can make for each child, so it
    // can carry what the caller needs downwards (a path, an owner). `for_each_child` calls its argument
    // with each child in order. `has_children` is asked first, so a collapsed node's children are never
    // made.
    template<typename Source, typename Node>
    concept TreeSource = requires(const Source &source, const Node &node) {
        source.key(node);
        { source.has_children(node) } -> std::convertible_to<bool>;
        source.for_each_child(node, [](const Node &) {});
    };

    namespace detail {

        template<typename Node, typename Key, typename Hash, typename Source, typename Row>
        void flatten_node(const Node &node, int depth, std::size_t parent, const TreeExpansion<Key, Hash> &expansion,
                          const Source &source, Row &row, std::vector<TreeRowInfo> &out) {
            TreeRowInfo info;
            info.depth = depth;
            info.parent = parent;
            info.has_children = source.has_children(node);
            info.expanded = info.has_children && expansion.is_expanded(source.key(node));
            const std::size_t index = out.size();
            out.push_back(info);
            row(node, info);
            if (!info.expanded) {
                return;
            }
            source.for_each_child(node, [&](const Node &child) {
                flatten_node(child, depth + 1, index, expansion, source, row, out);
            });
        }

    } // namespace detail

    // The rows a tree shows: every root and every node whose ancestors are all expanded, depth first.
    // Calls `row(node, info)` once per row, in order, so a caller can build its own row beside each info.
    // A collapsed node's subtree is not walked.
    template<std::ranges::input_range Roots, typename Key, typename Hash, typename Source, typename Row>
        requires TreeSource<Source, std::ranges::range_value_t<Roots>>
    [[nodiscard]] std::vector<TreeRowInfo> flatten_tree(const Roots &roots, const TreeExpansion<Key, Hash> &expansion,
                                                        const Source &source, Row &&row) {
        std::vector<TreeRowInfo> out;
        for (const auto &root: roots) {
            detail::flatten_node(root, 0, kNoTreeRow, expansion, source, row, out);
        }
        return out;
    }

    enum class TreeNav {
        Up,
        Down,
        // Collapses an expanded row, otherwise moves to the parent.
        Left,
        // Expands a collapsed row, otherwise moves to its first child.
        Right,
        First,
        Last,
    };

    // The tree key of a list: arrows, Home, and End. A tree reads KeyEvent, auto-repeat included, the way
    // other UI does; these are not gameplay actions.
    [[nodiscard]] constexpr std::optional<TreeNav> tree_nav_for_key(KeyCode key) {
        switch (key) {
            case KeyCode::Up:
                return TreeNav::Up;
            case KeyCode::Down:
                return TreeNav::Down;
            case KeyCode::Left:
                return TreeNav::Left;
            case KeyCode::Right:
                return TreeNav::Right;
            case KeyCode::Home:
                return TreeNav::First;
            case KeyCode::End:
                return TreeNav::Last;
            default:
                return std::nullopt;
        }
    }

    // What a navigation key does to the rows: select `row`, and when `toggle` is set, also expand or
    // collapse it. `row` is kNoTreeRow when there are no rows.
    struct TreeNavResult {
        std::size_t row = kNoTreeRow;
        bool toggle = false;

        bool operator==(const TreeNavResult &) const = default;
    };

    // `current` is the selected row, or kNoTreeRow. With nothing selected, Down and First go to the first
    // row and Up and Last to the last; Left and Right select nothing.
    [[nodiscard]] inline TreeNavResult tree_navigate(std::span<const TreeRowInfo> rows, std::size_t current,
                                                     TreeNav nav) {
        if (rows.empty()) {
            return {};
        }
        const std::size_t last = rows.size() - 1;
        if (current >= rows.size()) {
            switch (nav) {
                case TreeNav::Down:
                case TreeNav::First:
                    return {0};
                case TreeNav::Up:
                case TreeNav::Last:
                    return {last};
                case TreeNav::Left:
                case TreeNav::Right:
                    return {};
            }
            return {};
        }
        const TreeRowInfo &row = rows[current];
        switch (nav) {
            case TreeNav::Up:
                return {current == 0 ? 0 : current - 1};
            case TreeNav::Down:
                return {current == last ? last : current + 1};
            case TreeNav::First:
                return {0};
            case TreeNav::Last:
                return {last};
            case TreeNav::Left:
                if (row.expanded) {
                    return {current, true};
                }
                return {row.parent == kNoTreeRow ? current : row.parent};
            case TreeNav::Right:
                if (row.has_children && !row.expanded) {
                    return {current, true};
                }
                if (row.expanded && current < last) {
                    return {current + 1};
                }
                return {current};
        }
        return {current};
    }

} // namespace engine::ui
