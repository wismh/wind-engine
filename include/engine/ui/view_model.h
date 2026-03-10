#pragma once

// docs/tech/modules/UI.md

#include <engine/resources/asset_id.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/command.h>
#include <engine/ui/paint.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ui {

class ViewModel {
public:
    ViewModel() : serial_(++g_serial) {}
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&) = delete;
    ViewModel& operator=(const ViewModel&) = delete;
    ViewModel(ViewModel&&) = delete;
    ViewModel& operator=(ViewModel&&) = delete;

    // A number no other view-model of this process has had, never reused: what an ItemsControl row remembers of the
    // item it was built for. The address of an item is not enough, a view-model destroyed and another created in its
    // place share it.
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }

    // Goes up whenever `property`, `command` or `paint` registers something, a replacement included. The bind pass
    // reads the view-model's registrations when it builds its table, and builds it again when this moved, so a
    // binding registered late is found and a paint replaced is read again.
    [[nodiscard]] std::uint64_t registration_version() const noexcept { return registration_version_; }

    [[nodiscard]] bool has_property(BindingId id) const;
    [[nodiscard]] bool has_command(BindingId id) const;
    [[nodiscard]] bool has_paint(BindingId id) const;
    [[nodiscard]] std::optional<std::string> read_property_string(BindingId id) const;
    // Copies the property into `dest` only when the text differs. A `Bindable<std::string>` is compared
    // in place. A number formatted as text is formatted first, then copied only when that text differs.
    // Returns false when `id` is not a string-producing property and leaves `dest` unchanged.
    bool assign_property_string(BindingId id, std::string& dest) const;

    enum class StringBind : std::uint8_t {
        // `id` is not a string-producing property; `dest` is untouched.
        Missing,
        // `dest` already held the property's text.
        Unchanged,
        // `dest` was overwritten.
        Changed,
    };
    // `assign_property_string` that says whether it wrote.
    [[nodiscard]] StringBind bind_property_string(BindingId id, std::string &dest) const;

    // Where the version of a property can be read without a lookup: the bind pass keeps one per slot and compares
    // `read()` with the version it last saw. Valid while this view-model lives and `id` is not registered again.
    struct PropertyVersion {
        const void *bindable = nullptr;
        std::uint64_t (*version)(const void *) = nullptr;
        [[nodiscard]] std::uint64_t read() const { return version(bindable); }
    };
    [[nodiscard]] std::optional<PropertyVersion> property_version(BindingId id) const;
    [[nodiscard]] std::optional<AssetId> read_property_asset_id(BindingId id) const;
    [[nodiscard]] std::optional<float> read_property_float(BindingId id) const;
    // Writes through to the Bindable<T> registered for `id`, if T is arithmetic (populated by
    // property<T>() below). Returns false and leaves the property untouched when `id` isn't
    // registered or was registered with a non-arithmetic T (e.g. Bindable<std::string>) — the
    // same graceful no-op this codebase already uses for other malformed/unsupported bindings.
    bool write_property_float(BindingId id, float value);
    // Writes through to the Bindable<std::string> registered for `id`. Returns false and leaves
    // the property untouched when `id` isn't registered or was registered with a non-string T.
    bool write_property_string(BindingId id, std::string_view value);
    [[nodiscard]] std::vector<ViewModel*> read_item_source(BindingId id) const;
    [[nodiscard]] ICommand* find_command(BindingId id);
    [[nodiscard]] const ICommand* find_command(BindingId id) const;
    [[nodiscard]] IPaint* find_paint(BindingId id);
    [[nodiscard]] const IPaint* find_paint(BindingId id) const;

    template<typename T>
    void property(BindingId id, Bindable<T>& bindable);

    template<typename T>
    void property(BindingId id, BindableList<std::shared_ptr<T>>& list);

    void command(BindingId id, ICommand& command);
    void paint(BindingId id, IPaint& paint);

private:
    struct PropertyRef {
        void* bindable = nullptr;
        std::string (*to_string)(void*) = nullptr;
        bool (*store_string)(void *, std::string &) = nullptr;
        std::uint64_t (*version)(const void *) = nullptr;
        AssetId (*read_asset_id)(void*) = nullptr;
        float (*read_float)(void*) = nullptr;
        void (*write_float)(void*, float) = nullptr;
        void (*write_string)(void*, std::string_view) = nullptr;
        std::vector<ViewModel*> (*items)(void*) = nullptr;
    };

    static inline std::atomic<std::uint64_t> g_serial{0};

    std::uint64_t serial_ = 0;
    std::uint64_t registration_version_ = 0;
    std::unordered_map<BindingId, PropertyRef> properties_;
    std::unordered_map<BindingId, ICommand*> commands_;
    std::unordered_map<BindingId, IPaint*> paints_;
};

template<typename T>
void ViewModel::property(BindingId id, Bindable<T>& bindable) {
    PropertyRef ref;
    ref.bindable = &bindable;
    ref.to_string = [](void* ptr) -> std::string {
        const T& value = static_cast<Bindable<T>*>(ptr)->get();
        if constexpr (std::is_same_v<T, std::string>) {
            return value;
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value);
        } else {
            return {};
        }
    };
    ref.version = [](const void *ptr) { return static_cast<const Bindable<T> *>(ptr)->version(); };
    if constexpr (std::is_same_v<T, std::string>) {
        ref.store_string = [](void* ptr, std::string& dest) {
            const std::string& value = static_cast<Bindable<std::string>*>(ptr)->get();
            if (dest != value) {
                dest = value;
                return true;
            }
            return false;
        };
    } else if constexpr (std::is_arithmetic_v<T>) {
        ref.store_string = [](void* ptr, std::string& dest) {
            std::string formatted = std::to_string(static_cast<Bindable<T>*>(ptr)->get());
            if (dest != formatted) {
                dest = std::move(formatted);
                return true;
            }
            return false;
        };
    } else {
        ref.store_string = [](void*, std::string& dest) {
            if (!dest.empty()) {
                dest.clear();
                return true;
            }
            return false;
        };
    }
    if constexpr (std::is_same_v<T, std::string>) {
        ref.write_string = [](void* ptr, std::string_view value) {
            static_cast<Bindable<std::string>*>(ptr)->set(std::string(value));
        };
    }
    if constexpr (std::is_same_v<T, AssetId>) {
        ref.read_asset_id = [](void* ptr) -> AssetId {
            return static_cast<Bindable<AssetId>*>(ptr)->get();
        };
    }
    if constexpr (std::is_arithmetic_v<T>) {
        ref.read_float = [](void* ptr) -> float {
            return static_cast<float>(static_cast<Bindable<T>*>(ptr)->get());
        };
        ref.write_float = [](void* ptr, float value) {
            static_cast<Bindable<T>*>(ptr)->set(static_cast<T>(value));
        };
    }
    properties_.insert_or_assign(id, ref);
    ++registration_version_;
}

template<typename T>
void ViewModel::property(BindingId id, BindableList<std::shared_ptr<T>>& list) {
    static_assert(std::is_base_of_v<ViewModel, T>, "items_source items must be ViewModels");
    PropertyRef ref;
    ref.bindable = &list;
    ref.version = [](const void *ptr) { return static_cast<const BindableList<std::shared_ptr<T>> *>(ptr)->version(); };
    ref.items = [](void* ptr) -> std::vector<ViewModel*> {
        auto& items = static_cast<BindableList<std::shared_ptr<T>>*>(ptr)->get();
        std::vector<ViewModel*> out;
        out.reserve(items.size());
        for (const std::shared_ptr<T>& item : items) {
            if (item) {
                out.push_back(item.get());
            }
        }
        return out;
    };
    properties_.insert_or_assign(id, ref);
    ++registration_version_;
}

}
