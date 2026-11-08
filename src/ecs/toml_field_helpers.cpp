#include "toml_field_helpers.h"

#include <cstdint>
#include <string>
#include <utility>

namespace engine::ecs::detail {

std::optional<FieldValue> toml_node_to_field(const toml::node& node) {
    if (const auto* b = node.as_boolean()) {
        return FieldValue{b->get()};
    }
    if (const auto* i = node.as_integer()) {
        return FieldValue{i->get()};
    }
    if (const auto* f = node.as_floating_point()) {
        return FieldValue{f->get()};
    }
    if (const auto* s = node.as_string()) {
        return FieldValue{s->get()};
    }
    if (const toml::array* arr = node.as_array()) {
        FieldArray items;
        items.reserve(arr->size());
        for (const toml::node& elem : *arr) {
            auto converted = toml_node_to_field(elem);
            if (!converted) {
                return std::nullopt;
            }
            items.push_back(std::move(*converted));
        }
        return FieldValue{std::move(items)};
    }
    if (const toml::table* tab = node.as_table()) {
        auto converted = toml_table_to_field_table(*tab);
        if (!converted) {
            return std::nullopt;
        }
        return FieldValue{std::move(*converted)};
    }
    return std::nullopt;
}

std::optional<FieldTable> toml_table_to_field_table(const toml::table& table) {
    FieldTable out;
    for (const auto& [k, v] : table) {
        auto converted = toml_node_to_field(v);
        if (!converted) {
            return std::nullopt;
        }
        out.emplace(std::string(k.str()), std::move(*converted));
    }
    return out;
}

void insert_field_into_array(const FieldValue& val, toml::array& arr) {
    if (const auto* i = val.as_int()) {
        arr.push_back(*i);
    } else if (const auto* b = val.as_bool()) {
        arr.push_back(*b);
    } else if (const auto* s = val.as_string()) {
        arr.push_back(*s);
    } else if (const auto* sub_arr = val.as_array()) {
        toml::array child;
        child.reserve(sub_arr->size());
        for (const FieldValue& elem : *sub_arr) {
            insert_field_into_array(elem, child);
        }
        arr.push_back(std::move(child));
    } else if (const auto* sub_tab = val.as_table()) {
        toml::table child;
        field_to_toml_table(*sub_tab, child);
        child.is_inline(true);
        arr.push_back(std::move(child));
    } else if (const auto num = val.as_number()) {
        arr.push_back(*num);
    }
}

void field_to_toml_table(const FieldTable& src, toml::table& dst) {
    for (const auto& [k, val] : src) {
        if (const auto* i = val.as_int()) {
            dst.insert_or_assign(k, *i);
        } else if (const auto* b = val.as_bool()) {
            dst.insert_or_assign(k, *b);
        } else if (const auto* s = val.as_string()) {
            dst.insert_or_assign(k, *s);
        } else if (const auto* sub_arr = val.as_array()) {
            toml::array child;
            child.reserve(sub_arr->size());
            for (const FieldValue& elem : *sub_arr) {
                insert_field_into_array(elem, child);
            }
            dst.insert_or_assign(k, std::move(child));
        } else if (const auto* sub_tab = val.as_table()) {
            toml::table child;
            field_to_toml_table(*sub_tab, child);
            child.is_inline(true);
            dst.insert_or_assign(k, std::move(child));
        } else if (const auto num = val.as_number()) {
            dst.insert_or_assign(k, *num);
        }
    }
}

} // namespace engine::ecs::detail
