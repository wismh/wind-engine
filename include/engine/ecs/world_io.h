#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/asset_ref.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Marks a game component struct for build-time serialization codegen (`component_io.h`).
#define WIND_COMPONENT

// Marks a game value struct for the same codegen. A field of this type inside a component is stored in that
// component's table. A `.object` file whose `type` is this struct's key is loaded with `AssetsDb::get<T>`.
#define WIND_SERIALIZABLE

namespace engine {
class AssetsDb;
}

namespace engine::ecs {

struct EntityName {
    std::string id;
};

struct FieldValue;
using FieldArray = std::vector<FieldValue>;
using FieldTable = std::map<std::string, FieldValue, std::less<>>;

struct FieldValue {
    using Storage = std::variant<std::int64_t, double, bool, std::string, FieldArray, FieldTable>;
    Storage data{std::int64_t{0}};

    FieldValue() = default;
    FieldValue(std::int64_t v) : data(v) {}
    FieldValue(int v) : data(static_cast<std::int64_t>(v)) {}
    FieldValue(double v) : data(v) {}
    FieldValue(float v) : data(static_cast<double>(v)) {}
    FieldValue(bool v) : data(v) {}
    FieldValue(std::string v) : data(std::move(v)) {}
    FieldValue(std::string_view v) : data(std::string(v)) {}
    FieldValue(const char* v) : data(std::string(v)) {}
    FieldValue(FieldArray v) : data(std::move(v)) {}
    FieldValue(FieldTable v) : data(std::move(v)) {}

    [[nodiscard]] const std::int64_t* as_int() const noexcept { return std::get_if<std::int64_t>(&data); }
    [[nodiscard]] std::optional<double> as_number() const noexcept {
        if (const auto* f = std::get_if<double>(&data)) {
            return *f;
        }
        if (const auto* i = std::get_if<std::int64_t>(&data)) {
            return static_cast<double>(*i);
        }
        return std::nullopt;
    }
    [[nodiscard]] const bool* as_bool() const noexcept { return std::get_if<bool>(&data); }
    [[nodiscard]] const std::string* as_string() const noexcept { return std::get_if<std::string>(&data); }
    [[nodiscard]] const FieldArray* as_array() const noexcept { return std::get_if<FieldArray>(&data); }
    [[nodiscard]] const FieldTable* as_table() const noexcept { return std::get_if<FieldTable>(&data); }

    bool operator==(const FieldValue&) const = default;
};

class ComponentWriter {
public:
    using EntityIdResolver = std::function<std::string(Entity)>;

    explicit ComponentWriter(FieldTable& table, EntityIdResolver entity_to_id = {}) :
        table_(table), entity_to_id_(std::move(entity_to_id)) {}

    // A nested value shares this writer's entity resolver. `table` is the caller's storage.
    [[nodiscard]] ComponentWriter nested(FieldTable& table) const { return ComponentWriter{table, entity_to_id_}; }

    [[nodiscard]] FieldTable& table() noexcept { return table_; }

    template<typename T>
    void field(std::string_view name, const std::optional<T>& value) {
        if (value.has_value()) {
            table_.insert_or_assign(std::string(name), encode(*value));
        }
    }

    template<typename T>
    void field(std::string_view name, const T& value) {
        table_.insert_or_assign(std::string(name), encode(value));
    }

    [[nodiscard]] FieldValue encode(bool value) const { return FieldValue{value}; }

    template<std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] FieldValue encode(T value) const {
        return FieldValue{static_cast<std::int64_t>(value)};
    }

    template<std::floating_point T>
    [[nodiscard]] FieldValue encode(T value) const {
        return FieldValue{static_cast<double>(value)};
    }

    [[nodiscard]] FieldValue encode(const std::string& value) const { return FieldValue{value}; }
    [[nodiscard]] FieldValue encode(std::string_view value) const { return FieldValue{std::string(value)}; }

    [[nodiscard]] FieldValue encode(const glm::vec2& v) const {
        return FieldValue{FieldArray{FieldValue{v.x}, FieldValue{v.y}}};
    }

    [[nodiscard]] FieldValue encode(const glm::vec3& v) const {
        return FieldValue{FieldArray{FieldValue{v.x}, FieldValue{v.y}, FieldValue{v.z}}};
    }

    [[nodiscard]] FieldValue encode(const glm::vec4& v) const {
        return FieldValue{FieldArray{FieldValue{v.x}, FieldValue{v.y}, FieldValue{v.z}, FieldValue{v.w}}};
    }

    [[nodiscard]] FieldValue encode(const glm::quat& q) const {
        return FieldValue{FieldArray{FieldValue{q.x}, FieldValue{q.y}, FieldValue{q.z}, FieldValue{q.w}}};
    }

    [[nodiscard]] FieldValue encode(const AssetId& id) const {
        if (id == AssetId{}) {
            return FieldValue{std::string{}};
        }
        return FieldValue{std::string(id.hex())};
    }

    template<typename T>
    [[nodiscard]] FieldValue encode(const AssetRef<T>& ref) const {
        return encode(ref.id());
    }

    [[nodiscard]] FieldValue encode(Entity entity) const {
        if (entity == Entity{} || !entity_to_id_) {
            return FieldValue{std::string{}};
        }
        return FieldValue{entity_to_id_(entity)};
    }

    template<typename T>
        requires std::is_enum_v<T>
    [[nodiscard]] FieldValue encode(T value) const {
        return FieldValue{static_cast<std::int64_t>(static_cast<std::underlying_type_t<T>>(value))};
    }

    template<typename T>
    [[nodiscard]] FieldValue encode(const std::vector<T>& items) const {
        FieldArray out;
        out.reserve(items.size());
        for (const T& item : items) {
            out.push_back(encode(item));
        }
        return FieldValue{std::move(out)};
    }

    // User structs opt in with a free `encode_wind` in their namespace (ADL). Generated for `WIND_SERIALIZABLE`.
    template<typename T>
        requires requires(const ComponentWriter& writer, const T& value) { encode_wind(writer, value); }
    [[nodiscard]] FieldValue encode(const T& value) const {
        return encode_wind(*this, value);
    }

private:
    FieldTable& table_;
    EntityIdResolver entity_to_id_;
};

class ComponentReader {
public:
    using EntityLookup = std::function<std::optional<Entity>(std::string_view)>;

    explicit ComponentReader(const FieldTable& table, EntityLookup id_to_entity = {}, AssetsDb* assets = nullptr) :
        table_(table), id_to_entity_(std::move(id_to_entity)), assets_(assets) {}

    // A nested value shares this reader's entity lookup and asset database.
    [[nodiscard]] ComponentReader nested(const FieldTable& table) const {
        return ComponentReader{table, id_to_entity_, assets_};
    }

    [[nodiscard]] const FieldTable& table() const noexcept { return table_; }
    [[nodiscard]] AssetsDb* assets() const noexcept { return assets_; }
    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
    [[nodiscard]] std::string_view error() const noexcept { return error_; }

    void fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    template<typename T>
    bool field(std::string_view name, std::optional<T>& value) {
        const auto it = table_.find(name);
        if (it == table_.end()) {
            return true;
        }
        T decoded{};
        if (!decode(it->second, decoded)) {
            fail("invalid field \"" + std::string(name) + "\"");
            return false;
        }
        value = std::move(decoded);
        return true;
    }

    template<typename T>
    bool field(std::string_view name, T& value) {
        const auto it = table_.find(name);
        if (it == table_.end()) {
            return true;
        }
        if (!decode(it->second, value)) {
            fail("invalid field \"" + std::string(name) + "\"");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool decode(const FieldValue& node, bool& out) const {
        if (const bool* b = node.as_bool()) {
            out = *b;
            return true;
        }
        return false;
    }

    template<std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] bool decode(const FieldValue& node, T& out) const {
        const std::int64_t* i = node.as_int();
        if (i == nullptr) {
            return false;
        }
        if constexpr (std::is_unsigned_v<T>) {
            if (*i < 0) {
                return false;
            }
        }
        out = static_cast<T>(*i);
        return true;
    }

    template<std::floating_point T>
    [[nodiscard]] bool decode(const FieldValue& node, T& out) const {
        if (const auto n = node.as_number()) {
            out = static_cast<T>(*n);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, std::string& out) const {
        if (const std::string* s = node.as_string()) {
            out = *s;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, glm::vec2& out) const {
        if (const FieldArray* arr = node.as_array()) {
            if (arr->size() != 2) {
                return false;
            }
            const auto x = (*arr)[0].as_number();
            const auto y = (*arr)[1].as_number();
            if (!x || !y) {
                return false;
            }
            out = glm::vec2{static_cast<float>(*x), static_cast<float>(*y)};
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, glm::vec3& out) const {
        if (const FieldArray* arr = node.as_array()) {
            if (arr->size() != 3) {
                return false;
            }
            const auto x = (*arr)[0].as_number();
            const auto y = (*arr)[1].as_number();
            const auto z = (*arr)[2].as_number();
            if (!x || !y || !z) {
                return false;
            }
            out = glm::vec3{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, glm::vec4& out) const {
        if (const FieldArray* arr = node.as_array()) {
            if (arr->size() != 4) {
                return false;
            }
            const auto x = (*arr)[0].as_number();
            const auto y = (*arr)[1].as_number();
            const auto z = (*arr)[2].as_number();
            const auto w = (*arr)[3].as_number();
            if (!x || !y || !z || !w) {
                return false;
            }
            out = glm::vec4{
                    static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z), static_cast<float>(*w)};
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, glm::quat& out) const {
        if (const FieldArray* arr = node.as_array()) {
            if (arr->size() != 4) {
                return false;
            }
            const auto x = (*arr)[0].as_number();
            const auto y = (*arr)[1].as_number();
            const auto z = (*arr)[2].as_number();
            const auto w = (*arr)[3].as_number();
            if (!x || !y || !z || !w) {
                return false;
            }
            out = glm::quat{
                    static_cast<float>(*w), static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
            return true;
        }
        return false;
    }

    [[nodiscard]] bool decode(const FieldValue& node, AssetId& out) const {
        const std::string* s = node.as_string();
        if (s == nullptr) {
            return false;
        }
        if (s->empty()) {
            out = AssetId{};
            return true;
        }
        const auto parsed = AssetId::parse(*s);
        if (!parsed) {
            return false;
        }
        out = *parsed;
        return true;
    }

    template<typename T>
    [[nodiscard]] bool decode(const FieldValue& node, AssetRef<T>& out) const {
        AssetId temp;
        if (!decode(node, temp)) {
            return false;
        }
        out = AssetRef<T>{temp};
        return true;
    }

    [[nodiscard]] bool decode(const FieldValue& node, Entity& out) const {
        const std::string* s = node.as_string();
        if (s == nullptr) {
            return false;
        }
        if (s->empty()) {
            out = Entity{};
            return true;
        }
        if (!id_to_entity_) {
            return false;
        }
        const auto resolved = id_to_entity_(*s);
        if (!resolved) {
            return false;
        }
        out = *resolved;
        return true;
    }

    template<typename T>
        requires std::is_enum_v<T>
    [[nodiscard]] bool decode(const FieldValue& node, T& out) const {
        const std::int64_t* i = node.as_int();
        if (i == nullptr) {
            return false;
        }
        out = static_cast<T>(static_cast<std::underlying_type_t<T>>(*i));
        return true;
    }

    template<typename T>
    [[nodiscard]] bool decode(const FieldValue& node, std::vector<T>& out) const {
        const FieldArray* arr = node.as_array();
        if (arr == nullptr) {
            return false;
        }
        std::vector<T> result;
        result.reserve(arr->size());
        for (const FieldValue& elem : *arr) {
            T decoded{};
            if (!decode(elem, decoded)) {
                return false;
            }
            result.push_back(std::move(decoded));
        }
        out = std::move(result);
        return true;
    }

    // User structs opt in with a free `decode_wind` in their namespace (ADL). Generated for `WIND_SERIALIZABLE`.
    template<typename T>
        requires requires(const ComponentReader& reader, const FieldValue& node, T& value) {
            decode_wind(reader, node, value);
        }
    [[nodiscard]] bool decode(const FieldValue& node, T& out) const {
        return decode_wind(*this, node, out);
    }

private:
    const FieldTable& table_;
    EntityLookup id_to_entity_;
    AssetsDb* assets_ = nullptr;
    std::string error_;
};

} // namespace engine::ecs
