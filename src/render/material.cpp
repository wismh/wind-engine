#include <engine/render/material.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::render {

void MaterialOverride::set_vec4(std::string_view name, const glm::vec4& value) {
    const auto it = std::find_if(vec4_params.begin(), vec4_params.end(),
            [name](const auto& param) { return param.first == name; });
    if (it != vec4_params.end()) {
        it->second = value;
    } else {
        vec4_params.emplace_back(std::string(name), value);
    }
}

namespace {

bool is_hex_guid(std::string_view value) {
    if (value.size() != 32) {
        return false;
    }
    for (char c : value) {
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

std::optional<float> node_as_float(const toml::node& node) {
    if (const auto f = node.value<double>()) {
        return static_cast<float>(*f);
    }
    if (const auto i = node.value<std::int64_t>()) {
        return static_cast<float>(*i);
    }
    return std::nullopt;
}

std::optional<BlendMode> parse_blend(std::string_view value) {
    if (value == "opaque") {
        return BlendMode::Opaque;
    }
    if (value == "alpha") {
        return BlendMode::Alpha;
    }
    if (value == "additive") {
        return BlendMode::Additive;
    }
    return std::nullopt;
}

std::optional<CullMode> parse_cull(std::string_view value) {
    if (value == "none") {
        return CullMode::None;
    }
    if (value == "back") {
        return CullMode::Back;
    }
    if (value == "front") {
        return CullMode::Front;
    }
    return std::nullopt;
}

// Absent key: `fallback`. Present but not a boolean: nullopt (the material fails to parse).
std::optional<bool> bool_key(const toml::table& table, std::string_view key, bool fallback) {
    const toml::node* const node = table.get(key);
    if (node == nullptr) {
        return fallback;
    }
    // `as_boolean`, not `value<bool>`: the latter converts an integer.
    if (const toml::value<bool>* const flag = node->as_boolean()) {
        return flag->get();
    }
    return std::nullopt;
}

std::optional<MaterialDesc> parse_material_table(const toml::table& table) {
    MaterialDesc desc;

    const auto shader = table["shader"].value<std::string>();
    if (!shader || !is_hex_guid(*shader)) {
        return std::nullopt;
    }
    desc.shader = *shader;

    if (const auto blend = table["blend"].value<std::string>()) {
        const auto parsed = parse_blend(*blend);
        if (!parsed) {
            return std::nullopt;
        }
        desc.state.blend = *parsed;
    }

    // `depth` sets both depth keys; `depth_test` / `depth_write` override it one by one.
    const auto depth = bool_key(table, "depth", false);
    if (!depth) {
        return std::nullopt;
    }
    const auto depth_test = bool_key(table, "depth_test", *depth);
    const auto depth_write = bool_key(table, "depth_write", *depth);
    if (!depth_test || !depth_write) {
        return std::nullopt;
    }
    desc.state.depth_test = *depth_test;
    desc.state.depth_write = *depth_write;

    if (const toml::node* cull_node = table.get("cull")) {
        const auto cull_name = cull_node->value<std::string>();
        if (!cull_name) {
            return std::nullopt;
        }
        const auto cull = parse_cull(*cull_name);
        if (!cull) {
            return std::nullopt;
        }
        desc.state.cull = *cull;
    }

    if (const toml::node* color_node = table.get("color")) {
        const toml::array* const arr = color_node->as_array();
        if (arr == nullptr || arr->size() != 4) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            const toml::node* const component = arr->get(i);
            if (component == nullptr) {
                return std::nullopt;
            }
            const auto value = node_as_float(*component);
            if (!value) {
                return std::nullopt;
            }
            desc.color[static_cast<glm::length_t>(i)] = *value;
        }
    }

    if (const toml::table* textures = table["textures"].as_table()) {
        if (const auto albedo = (*textures)["albedo"].value<std::string>()) {
            if (!is_hex_guid(*albedo)) {
                return std::nullopt;
            }
            desc.albedo = *albedo;
        }
    }

    return desc;
}

}

std::optional<MaterialDesc> parse_material(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        return parse_material_table(table);
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

std::optional<MaterialDesc> parse_material_file(std::string_view path) {
    try {
        const toml::table table = toml::parse_file(path);
        return parse_material_table(table);
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

}
