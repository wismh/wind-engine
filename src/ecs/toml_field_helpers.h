#pragma once

#include <engine/ecs/world_io.h>

#include <toml++/toml.hpp>

#include <optional>

namespace engine::ecs::detail {

[[nodiscard]] std::optional<FieldValue> toml_node_to_field(const toml::node& node);
[[nodiscard]] std::optional<FieldTable> toml_table_to_field_table(const toml::table& table);
void field_to_toml_table(const FieldTable& src, toml::table& dst);
void insert_field_into_array(const FieldValue& val, toml::array& arr);

} // namespace engine::ecs::detail
