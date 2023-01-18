#pragma once

#include <engine/ecs/entity.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ecs {

template<typename... Ts>
class View;

class World {
    template<typename...>
    friend class View;

    class IPool {
    public:
        virtual ~IPool() = default;
        virtual void remove(Entity entity) = 0;
        virtual bool contains(Entity entity) const = 0;
        virtual std::size_t size() const = 0;
        virtual const Entity* packed() const = 0;
    };

    template<typename T>
    class Pool final : public IPool {
    public:
        template<typename... Args>
        T& emplace(Entity entity, Args&&... args);

        T* try_get(Entity entity);
        const T* try_get(Entity entity) const;

        void remove(Entity entity) override;
        bool contains(Entity entity) const override;
        std::size_t size() const override;
        const Entity* packed() const override;

    private:
        static constexpr std::uint32_t kTombstone = 0xFFFFFFFFu;

        void ensure_sparse(std::uint32_t index);

        std::vector<std::uint32_t> sparse_;
        std::vector<Entity> packed_;
        std::vector<T> dense_;
    };

public:
    World() = default;

    [[nodiscard]] Entity create();
    void destroy(Entity entity);
    [[nodiscard]] bool valid(Entity entity) const;

    template<typename T, typename... Args>
    T& emplace(Entity entity, Args&&... args);

    template<typename T>
    T& get(Entity entity);

    template<typename T>
    const T& get(Entity entity) const;

    template<typename T>
    [[nodiscard]] T* try_get(Entity entity);

    template<typename T>
    [[nodiscard]] const T* try_get(Entity entity) const;

    template<typename T>
    void remove(Entity entity);

    template<typename... Ts>
    [[nodiscard]] View<Ts...> view();

private:
    void begin_view();
    void end_view();
    void destroy_immediate(Entity entity);
    void flush_destroyed();

    template<typename T>
    Pool<T>& assure_pool();

    template<typename T>
    Pool<T>* pool_or_null() noexcept;

    template<typename T>
    const Pool<T>* pool_or_null() const noexcept;

    std::vector<std::uint32_t> generations_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint32_t> free_list_;
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> pools_;
    std::vector<Entity> pending_destroy_;
    int view_depth_ = 0;
};

template<typename... Ts>
class View {
    static_assert(sizeof...(Ts) >= 1, "view requires at least one component type");
    friend class World;

public:
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    View(View&& other) noexcept;
    View& operator=(View&& other) noexcept;
    ~View();

    class Iterator {
    public:
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        Entity operator*() const;
        Iterator& operator++();
        bool operator==(const Iterator& other) const noexcept;

    private:
        friend class View;

        Iterator(const View* view, std::size_t index, std::size_t end, const Entity* lead);

        void skip_mismatches();

        const View* view_ = nullptr;
        const Entity* lead_ = nullptr;
        std::size_t index_ = 0;
        std::size_t end_ = 0;
    };

    Iterator begin() const;
    Iterator end() const;

    template<typename T>
    T& get(Entity entity) const;

    template<typename Fn>
    void each(Fn&& fn) const;

private:
    explicit View(World& world);

    bool contains_all(Entity entity) const;
    World::IPool* pick_lead() const;

    World* world_ = nullptr;
    std::tuple<World::Pool<Ts>*...> pools_{};
    World::IPool* lead_ = nullptr;
    const Entity* lead_packed_ = nullptr;
    std::size_t lead_size_ = 0;
    bool owns_iteration_ = false;
};

}

#include <engine/ecs/world.inl>
