#pragma once

#include <engine/resources/asset_filter.h>

#include <array>
#include <string>
#include <vector>

namespace editor {

// How one field of a component is edited. Text is a read-only line (`text` is the whole row).
enum class InspectorFieldKind { Text, Bool, String, Number, Vec, Asset };

struct InspectorField {
    // Path written by an edit (`heroes[0].base.max_hp`). A top-level field's path is its name.
    std::string key;
    // Leaf shown on the row (`max_hp`, `weights[5]`). Empty uses `key`.
    std::string label;
    InspectorFieldKind kind = InspectorFieldKind::Text;
    // A read-only row, or the string / number in the text field.
    std::string text;
    bool flag = false;
    // Vec, and only the first `axis_count` entries (2, 3, or 4).
    std::array<std::string, 4> axes{};
    std::array<bool, 4> axis_integer{};
    int axis_count = 0;
    bool integer = false;
    bool editable = false;
    std::string asset_name;
    engine::AssetFilter asset_filter{};

    bool operator==(const InspectorField&) const = default;
};

// One section of the Inspector tab as plain data: a heading, read-only lines, and editable fields. The panel
// turns it into view-models and remembers by heading which sections the user collapsed. `removable` is a
// registered component on an entity in Edit Mode.
struct InspectorSection {
    std::string heading;
    std::vector<std::string> lines;
    std::vector<InspectorField> fields;
    bool removable = false;

    bool operator==(const InspectorSection&) const = default;
};

}
