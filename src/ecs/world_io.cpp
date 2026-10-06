#include <engine/ecs/world_io.h>

namespace engine::ecs {

void merge_field_tables(FieldTable& target, const FieldTable& overrides) {
    for (const auto& [key, val] : overrides) {
        auto it = target.find(key);
        if (it == target.end()) {
            target.insert_or_assign(key, val);
            continue;
        }

        const auto* override_tab = val.as_table();
        auto* target_tab = it->second.as_table();
        if (override_tab != nullptr && target_tab != nullptr) {
            merge_field_tables(*target_tab, *override_tab);
        } else {
            it->second = val;
        }
    }
}

FieldTable merged_field_tables(const FieldTable& base, const FieldTable& overrides) {
    FieldTable result = base;
    merge_field_tables(result, overrides);
    return result;
}

} // namespace engine::ecs
