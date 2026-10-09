#include <engine/ui/dock_layout.h>

#include <toml++/toml.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <utility>

namespace engine::ui {

namespace {

    constexpr std::int64_t kDockTextVersion = 1;

    // Shortest text that reads back as the same float through a double, the way toml++ parses it.
    void append_number(std::string &out, float value) {
        if (std::isnan(value)) {
            out += "nan";
            return;
        }
        if (std::isinf(value)) {
            out += value < 0.0f ? "-inf" : "inf";
            return;
        }
        std::array<char, 64> buffer{};
        auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        // strtod, not from_chars: Apple's libc++ has no floating-point from_chars before Xcode 16.3. to_chars left the
        // buffer zero past `end`, so the text is terminated.
        const double back = std::strtod(buffer.data(), nullptr);
        if (static_cast<float>(back) != value) {
            end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<double>(value)).ptr;
        }
        const std::string_view text(buffer.data(), static_cast<std::size_t>(end - buffer.data()));
        out += text;
        if (text.find_first_of(".eE") == std::string_view::npos) {
            out += ".0";
        }
    }

    void append_string(std::string &out, std::string_view value) {
        out += '"';
        for (const char c: value) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (byte < 0x20 || byte == 0x7f) {
                std::array<char, 8> escaped{};
                std::snprintf(escaped.data(), escaped.size(), "\\u%04X", static_cast<unsigned>(byte));
                out += escaped.data();
            } else {
                out += c;
            }
        }
        out += '"';
    }

    [[nodiscard]] std::optional<DockNodeId> read_id(const toml::table &table, std::string_view key) {
        const std::optional<std::int64_t> value = table[key].value_exact<std::int64_t>();
        if (!value || *value < 0 || *value > std::numeric_limits<DockNodeId>::max()) {
            return std::nullopt;
        }
        return static_cast<DockNodeId>(*value);
    }

    [[nodiscard]] std::optional<float> read_number(const toml::node *node) {
        if (node == nullptr) {
            return std::nullopt;
        }
        if (const toml::value<double> *d = node->as_floating_point()) {
            return static_cast<float>(d->get());
        }
        if (const toml::value<std::int64_t> *i = node->as_integer()) {
            return static_cast<float>(i->get());
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DockNode> read_node(const toml::table &table) {
        DockNode n;
        const std::optional<DockNodeId> id = read_id(table, "id");
        const std::optional<std::string> kind = table["kind"].value_exact<std::string>();
        if (!id || !kind) {
            return std::nullopt;
        }
        n.id = *id;
        if (*kind == "tabs") {
            n.kind = DockNodeKind::Tabs;
            const toml::array *panels = table["panels"].as_array();
            const std::optional<std::int64_t> active = table["active"].value_exact<std::int64_t>();
            if (panels == nullptr || !active || *active < 0) {
                return std::nullopt;
            }
            for (const toml::node &panel: *panels) {
                const std::optional<std::string> key = panel.value_exact<std::string>();
                if (!key) {
                    return std::nullopt;
                }
                n.panels.push_back(*key);
            }
            n.active = static_cast<std::size_t>(*active);
            return n;
        }
        if (*kind == "split") {
            n.kind = DockNodeKind::Split;
            const std::optional<std::string> axis = table["axis"].value_exact<std::string>();
            const std::optional<float> ratio = read_number(table.get("ratio"));
            const std::optional<DockNodeId> first = read_id(table, "first");
            const std::optional<DockNodeId> second = read_id(table, "second");
            if (!axis || (*axis != "horizontal" && *axis != "vertical") || !ratio || !first || !second) {
                return std::nullopt;
            }
            n.axis = *axis == "horizontal" ? DockAxis::Horizontal : DockAxis::Vertical;
            n.ratio = *ratio;
            n.first = *first;
            n.second = *second;
            return n;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DockFloat> read_float(const toml::table &table) {
        const std::optional<DockNodeId> id = read_id(table, "id");
        const std::optional<DockNodeId> root = read_id(table, "root");
        const toml::array *rect = table["rect"].as_array();
        if (!id || !root || rect == nullptr || rect->size() != 4) {
            return std::nullopt;
        }
        std::array<float, 4> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::optional<float> v = read_number(rect->get(i));
            if (!v) {
                return std::nullopt;
            }
            values[i] = *v;
        }
        return DockFloat{*id, *root, {values[0], values[1], values[2], values[3]}};
    }

    // Parent links are not stored; they follow from the split children.
    void link_parents(std::map<DockNodeId, DockNode> &nodes) {
        for (const auto &[id, n]: nodes) {
            if (n.kind != DockNodeKind::Split) {
                continue;
            }
            for (const DockNodeId child: {n.first, n.second}) {
                const auto it = nodes.find(child);
                if (it != nodes.end()) {
                    it->second.parent = id;
                }
            }
        }
    }

} // namespace

std::string dock_layout_to_text(const DockLayout &layout) {
    std::string out;
    out += "version = " + std::to_string(kDockTextVersion) + "\n";
    out += "next_id = " + std::to_string(layout.next_id()) + "\n";
    out += "root = " + std::to_string(layout.root()) + "\n";
    for (const auto &[id, n]: layout.nodes()) {
        out += "\n[[node]]\nid = " + std::to_string(id) + "\n";
        if (n.kind == DockNodeKind::Tabs) {
            out += "kind = \"tabs\"\npanels = [";
            for (std::size_t i = 0; i < n.panels.size(); ++i) {
                out += i == 0 ? "" : ", ";
                append_string(out, n.panels[i]);
            }
            out += "]\nactive = " + std::to_string(n.active) + "\n";
        } else {
            out += "kind = \"split\"\naxis = \"";
            out += n.axis == DockAxis::Horizontal ? "horizontal" : "vertical";
            out += "\"\nratio = ";
            append_number(out, n.ratio);
            out += "\nfirst = " + std::to_string(n.first) + "\nsecond = " + std::to_string(n.second) + "\n";
        }
    }
    for (const DockFloat &f: layout.floats()) {
        out += "\n[[float]]\nid = " + std::to_string(f.id) + "\nroot = " + std::to_string(f.root) + "\nrect = [";
        const std::array<float, 4> rect{f.rect.x, f.rect.y, f.rect.w, f.rect.h};
        for (std::size_t i = 0; i < rect.size(); ++i) {
            out += i == 0 ? "" : ", ";
            append_number(out, rect[i]);
        }
        out += "]\n";
    }
    return out;
}

std::optional<DockLayout> dock_layout_from_text(std::string_view text) {
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error &) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> version = table["version"].value_exact<std::int64_t>();
    const std::optional<DockNodeId> next_id = read_id(table, "next_id");
    const std::optional<DockNodeId> root = read_id(table, "root");
    if (version != kDockTextVersion || !next_id || !root) {
        return std::nullopt;
    }
    std::map<DockNodeId, DockNode> nodes;
    if (const toml::node *list = table.get("node")) {
        const toml::array *items = list->as_array();
        if (items == nullptr) {
            return std::nullopt;
        }
        for (const toml::node &item: *items) {
            const toml::table *entry = item.as_table();
            std::optional<DockNode> n = entry != nullptr ? read_node(*entry) : std::nullopt;
            if (!n || !nodes.emplace(n->id, std::move(*n)).second) {
                return std::nullopt;
            }
        }
    }
    std::vector<DockFloat> floats;
    if (const toml::node *list = table.get("float")) {
        const toml::array *items = list->as_array();
        if (items == nullptr) {
            return std::nullopt;
        }
        for (const toml::node &item: *items) {
            const toml::table *entry = item.as_table();
            std::optional<DockFloat> f = entry != nullptr ? read_float(*entry) : std::nullopt;
            if (!f) {
                return std::nullopt;
            }
            floats.push_back(*f);
        }
    }
    link_parents(nodes);
    return DockLayout::from_parts(std::move(nodes), *root, std::move(floats), *next_id);
}

} // namespace engine::ui
