#pragma once

namespace engine::ecs {

template<typename T>
template<typename... Args>
T& World::Pool<T>::emplace(Entity entity, Args&&... args) {
    assert(!contains(entity));
    ensure_sparse(entity.index);
    sparse_[entity.index] = static_cast<std::uint32_t>(packed_.size());
    packed_.push_back(entity);
    dense_.emplace_back(std::forward<Args>(args)...);
    return dense_.back();
}

template<typename T>
T* World::Pool<T>::try_get(Entity entity) {
    if (!contains(entity)) {
        return nullptr;
    }
    return &dense_[sparse_[entity.index]];
}

template<typename T>
const T* World::Pool<T>::try_get(Entity entity) const {
    if (!contains(entity)) {
        return nullptr;
    }
    return &dense_[sparse_[entity.index]];
}

template<typename T>
void World::Pool<T>::remove(Entity entity) {
    if (!contains(entity)) {
        return;
    }
    const std::uint32_t packed_index = sparse_[entity.index];
    const std::uint32_t last = static_cast<std::uint32_t>(packed_.size() - 1);
    if (packed_index != last) {
        packed_[packed_index] = packed_[last];
        dense_[packed_index] = std::move(dense_[last]);
        sparse_[packed_[packed_index].index] = packed_index;
    }
    packed_.pop_back();
    dense_.pop_back();
    sparse_[entity.index] = kTombstone;
}

template<typename T>
bool World::Pool<T>::contains(Entity entity) const {
    if (entity.index >= sparse_.size()) {
        return false;
    }
    const std::uint32_t packed_index = sparse_[entity.index];
    if (packed_index == kTombstone || packed_index >= packed_.size()) {
        return false;
    }
    return packed_[packed_index] == entity;
}

template<typename T>
std::size_t World::Pool<T>::size() const {
    return packed_.size();
}

template<typename T>
const Entity* World::Pool<T>::packed() const {
    return packed_.data();
}

template<typename T>
void World::Pool<T>::ensure_sparse(std::uint32_t index) {
    if (index >= sparse_.size()) {
        sparse_.resize(static_cast<std::size_t>(index) + 1, kTombstone);
    }
}

template<typename T, typename... Args>
T& World::emplace(Entity entity, Args&&... args) {
    assert(valid(entity));
    return assure_pool<T>().emplace(entity, std::forward<Args>(args)...);
}

template<typename T>
T& World::get(Entity entity) {
    T* component = try_get<T>(entity);
    assert(component != nullptr);
    return *component;
}

template<typename T>
const T& World::get(Entity entity) const {
    const T* component = try_get<T>(entity);
    assert(component != nullptr);
    return *component;
}

template<typename T>
T* World::try_get(Entity entity) {
    Pool<T>* pool = pool_or_null<T>();
    return pool == nullptr ? nullptr : pool->try_get(entity);
}

template<typename T>
const T* World::try_get(Entity entity) const {
    const Pool<T>* pool = pool_or_null<T>();
    return pool == nullptr ? nullptr : pool->try_get(entity);
}

template<typename T>
void World::remove(Entity entity) {
    if (Pool<T>* pool = pool_or_null<T>()) {
        pool->remove(entity);
    }
}

template<typename... Ts>
View<Ts...> World::view() {
    return View<Ts...>(*this);
}

template<typename T>
T& World::ctx() {
    const std::type_index key{typeid(T)};
    auto it = resources_.find(key);
    if (it == resources_.end()) {
        auto resource = std::make_unique<Resource<T>>();
        T* value = &resource->value;
        resources_.emplace(key, std::move(resource));
        if constexpr (IsEvents<T>::value) {
            event_queues_.emplace_back([value]() { value->update(); });
        }
        return *value;
    }
    return static_cast<Resource<T>&>(*it->second).value;
}

template<typename T>
EventWriter<T>::EventWriter(World& world) : events_(&world.ctx<Events<T>>()) {}

template<typename T>
EventReader<T>::EventReader(World& world) : events_(&world.ctx<Events<T>>()) {}

template<typename T>
World::Pool<T>& World::assure_pool() {
    const std::type_index key{typeid(T)};
    auto it = pools_.find(key);
    if (it == pools_.end()) {
        auto pool = std::make_unique<Pool<T>>();
        Pool<T>& ref = *pool;
        pools_.emplace(key, std::move(pool));
        return ref;
    }
    return static_cast<Pool<T>&>(*it->second);
}

template<typename T>
World::Pool<T>* World::pool_or_null() noexcept {
    const std::type_index key{typeid(T)};
    auto it = pools_.find(key);
    if (it == pools_.end()) {
        return nullptr;
    }
    return static_cast<Pool<T>*>(it->second.get());
}

template<typename T>
const World::Pool<T>* World::pool_or_null() const noexcept {
    const std::type_index key{typeid(T)};
    auto it = pools_.find(key);
    if (it == pools_.end()) {
        return nullptr;
    }
    return static_cast<const Pool<T>*>(it->second.get());
}

template<typename... Ts>
View<Ts...>::View(World& world)
    : world_(&world)
    , pools_{world.pool_or_null<Ts>()...}
    , owns_iteration_(true) {
    world_->begin_view();
    if ((std::get<World::Pool<Ts>*>(pools_) && ...)) {
        lead_ = pick_lead();
        lead_packed_ = lead_->packed();
        lead_size_ = lead_->size();
    }
}

template<typename... Ts>
View<Ts...>::View(View&& other) noexcept
    : world_(other.world_)
    , pools_(other.pools_)
    , lead_(other.lead_)
    , lead_packed_(other.lead_packed_)
    , lead_size_(other.lead_size_)
    , owns_iteration_(other.owns_iteration_) {
    other.world_ = nullptr;
    other.owns_iteration_ = false;
}

template<typename... Ts>
View<Ts...>& View<Ts...>::operator=(View&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (owns_iteration_ && world_ != nullptr) {
        world_->end_view();
    }
    world_ = other.world_;
    pools_ = other.pools_;
    lead_ = other.lead_;
    lead_packed_ = other.lead_packed_;
    lead_size_ = other.lead_size_;
    owns_iteration_ = other.owns_iteration_;
    other.world_ = nullptr;
    other.owns_iteration_ = false;
    return *this;
}

template<typename... Ts>
View<Ts...>::~View() {
    if (owns_iteration_ && world_ != nullptr) {
        world_->end_view();
    }
}

template<typename... Ts>
View<Ts...>::Iterator::Iterator(const View* view, std::size_t index, std::size_t end, const Entity* lead)
    : view_(view)
    , lead_(lead)
    , index_(index)
    , end_(end) {
    skip_mismatches();
}

template<typename... Ts>
Entity View<Ts...>::Iterator::operator*() const {
    return lead_[index_];
}

template<typename... Ts>
typename View<Ts...>::Iterator& View<Ts...>::Iterator::operator++() {
    ++index_;
    skip_mismatches();
    return *this;
}

template<typename... Ts>
bool View<Ts...>::Iterator::operator==(const Iterator& other) const noexcept {
    return index_ == other.index_;
}

template<typename... Ts>
void View<Ts...>::Iterator::skip_mismatches() {
    while (index_ < end_ && !view_->contains_all(lead_[index_])) {
        ++index_;
    }
}

template<typename... Ts>
typename View<Ts...>::Iterator View<Ts...>::begin() const {
    return Iterator(this, 0, lead_size_, lead_packed_);
}

template<typename... Ts>
typename View<Ts...>::Iterator View<Ts...>::end() const {
    return Iterator(this, lead_size_, lead_size_, lead_packed_);
}

template<typename... Ts>
template<typename T>
T& View<Ts...>::get(Entity entity) const {
    return world_->get<T>(entity);
}

template<typename... Ts>
template<typename Fn>
void View<Ts...>::each(Fn&& fn) const {
    for (Entity entity : *this) {
        if constexpr (std::is_invocable_v<Fn, Entity, Ts&...>) {
            std::invoke(std::forward<Fn>(fn), entity, get<Ts>(entity)...);
        } else {
            std::invoke(std::forward<Fn>(fn), get<Ts>(entity)...);
        }
    }
}

template<typename... Ts>
bool View<Ts...>::contains_all(Entity entity) const {
    return (std::get<World::Pool<Ts>*>(pools_)->contains(entity) && ...);
}

template<typename... Ts>
World::IPool* View<Ts...>::pick_lead() const {
    World::IPool* lead = nullptr;
    std::size_t smallest = static_cast<std::size_t>(-1);
    auto consider = [&](World::IPool* pool) {
        if (pool->size() < smallest) {
            smallest = pool->size();
            lead = pool;
        }
    };
    (consider(std::get<World::Pool<Ts>*>(pools_)), ...);
    return lead;
}

}
