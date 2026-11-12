// A small entity-component system (ECS).
//
//   * An entity is an id. It has no data and no behaviour of its own.
//   * A component is a plain struct of data that is attached to an entity, such
//     as its Transform (see components.hpp). Any struct can be a component;
//     nothing has to be registered.
//   * A system is code that visits every entity having certain components. The
//     renderer, for example, draws every entity with a Transform and a
//     MeshRenderer.
//
// The World holds all of them:
//
//     World world;
//
//     Entity crate = world.create();
//     world.add(crate, Transform{{0.0f, 1.0f, 0.0f}});
//     world.add(crate, MeshRenderer{cubeMesh, {0.9f, 0.5f, 0.1f}});
//
//     world.get<Transform>(crate).position.y += 1.0f;
//
//     world.each<Transform, MeshRenderer>(
//         [](Entity entity, Transform& transform, MeshRenderer& renderer) {
//             // called once for every entity that has both components
//         });
//
//     world.destroy(crate);

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

// Identifies one entity of a World. Copy it around freely. Once the entity is
// destroyed its id is dead for good: World::alive() returns false for it, even
// after the World has reused the slot for a new entity.
struct Entity {
    std::uint32_t index      = UINT32_MAX; // slot in the World; the default is "no entity"
    std::uint32_t generation = 0;          // tells apart the entities that used the slot
};

inline bool operator==(Entity a, Entity b)
{
    return a.index == b.index && a.generation == b.generation;
}
inline bool operator!=(Entity a, Entity b) { return !(a == b); }

class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Explicit deep snapshots include every registered component pool and IDs.
    std::unique_ptr<World> snapshot() const {
        auto result = std::make_unique<World>();
        result->restore(*this);
        return result;
    }
    void restore(const World& source) {
        if (&source == this) return;
        std::vector<std::unique_ptr<PoolBase>> pools;
        for (const auto& pool : source.pools_)
            pools.push_back(pool ? pool->clone() : nullptr);
        generations_ = source.generations_;
        freeSlots_ = source.freeSlots_;
        pools_ = std::move(pools);
    }

    std::vector<Entity> entities() const {
        std::vector<bool> free(generations_.size(), false);
        for (auto index : freeSlots_) free[index] = true;
        std::vector<Entity> result;
        for (std::size_t i=0; i<generations_.size(); ++i)
            if (!free[i]) result.push_back({static_cast<std::uint32_t>(i), generations_[i]});
        return result;
    }

    // --- Entities -----------------------------------------------------------

    Entity create()
    {
        if (!freeSlots_.empty()) {
            const std::uint32_t index = freeSlots_.back();
            freeSlots_.pop_back();
            return {index, generations_[index]};
        }
        generations_.push_back(0);
        return {static_cast<std::uint32_t>(generations_.size() - 1), 0};
    }

    // Destroys the entity together with all its components. Does nothing if it
    // is already destroyed.
    void destroy(Entity entity)
    {
        if (!alive(entity)) {
            return;
        }
        for (const std::unique_ptr<PoolBase>& pool : pools_) {
            if (pool) {
                pool->remove(entity.index);
            }
        }
        ++generations_[entity.index]; // from now on the old id no longer matches
        freeSlots_.push_back(entity.index);
    }

    bool alive(Entity entity) const
    {
        return entity.index < generations_.size() &&
               generations_[entity.index] == entity.generation;
    }

    // --- Components ---------------------------------------------------------

    // Attaches a component to the entity, replacing the one of the same type if
    // it has one already:
    //
    //     world.add(entity, Transform{{0.0f, 1.0f, 0.0f}});
    //     world.add<RigidBody>(entity); // all default values
    template <typename T>
    T& add(Entity entity, T component = {})
    {
        if (!alive(entity)) {
            fail<T>("add() was called for an entity that does not exist (any more)");
        }
        return pool<T>().set(entity.index, std::move(component));
    }

    // Detaches the component. Does nothing if the entity does not have it.
    template <typename T>
    void remove(Entity entity)
    {
        if (alive(entity)) {
            pool<T>().remove(entity.index);
        }
    }

    template <typename T>
    bool has(Entity entity) const
    {
        const Pool<T>* found = findPool<T>();
        return found && alive(entity) && found->has(entity.index);
    }

    // The entity's component, or nullptr if it does not have one.
    template <typename T>
    T* tryGet(Entity entity)
    {
        Pool<T>& components = pool<T>();
        return alive(entity) && components.has(entity.index) ? &components.get(entity.index)
                                                              : nullptr;
    }

    // The entity's component. The entity must have it: use tryGet() or has()
    // when that is not certain.
    template <typename T>
    T& get(Entity entity)
    {
        T* component = tryGet<T>(entity);
        if (!component) {
            fail<T>(alive(entity)
                        ? "get() was called for an entity that does not have this component"
                        : "get() was called for an entity that does not exist (any more)");
        }
        return *component;
    }

    // --- Systems ------------------------------------------------------------

    // Calls fn(entity, component...) for every entity that has all the listed
    // components, in no particular order:
    //
    //     world.each<Transform, RigidBody>([](Entity entity, Transform& t, RigidBody& b) { ... });
    //
    // fn is free to create and destroy entities and to add and remove
    // components, on the entity it is visiting and on any other: an entity
    // that no longer has the components when its turn comes is skipped, and
    // the entities that fn creates are not visited by the same call. One rule
    // remains: the references handed to fn stop being valid as soon as fn adds
    // or removes a component of the same type on any entity.
    template <typename First, typename... Rest, typename Fn>
    void each(Fn&& fn)
    {
        // Adding and removing components moves the other components of the
        // pool around. So the entities are listed before the first call of
        // fn, and each one is looked up again when its turn comes.
        std::vector<Entity> entities;
        entities.reserve(pool<First>().owners.size());
        for (const std::uint32_t index : pool<First>().owners) {
            entities.push_back({index, generations_[index]});
        }
        for (const Entity entity : entities) {
            if (alive(entity) && pool<First>().has(entity.index) &&
                (pool<Rest>().has(entity.index) && ...)) {
                fn(entity, pool<First>().get(entity.index), pool<Rest>().get(entity.index)...);
            }
        }
    }

private:
    // All the components of one type, packed in an array. `slots` finds the
    // component of an entity without searching (this is called a sparse set).
    struct PoolBase {
        virtual ~PoolBase() = default;
        virtual void remove(std::uint32_t entityIndex) = 0;
        virtual std::unique_ptr<PoolBase> clone() const = 0;
    };

    template <typename T>
    struct Pool final : PoolBase {
        static constexpr std::uint32_t kNone = UINT32_MAX;

        std::vector<std::uint32_t> slots;      // entity index -> position in the arrays below
        std::vector<std::uint32_t> owners;     // entity index of each component
        std::vector<T>             components; // same order as `owners`

        std::unique_ptr<PoolBase> clone() const override { return std::make_unique<Pool<T>>(*this); }

        bool has(std::uint32_t entityIndex) const
        {
            return entityIndex < slots.size() && slots[entityIndex] != kNone;
        }

        T& get(std::uint32_t entityIndex) { return components[slots[entityIndex]]; }

        T& set(std::uint32_t entityIndex, T&& component)
        {
            if (has(entityIndex)) {
                return get(entityIndex) = std::move(component);
            }
            if (entityIndex >= slots.size()) {
                slots.resize(entityIndex + 1, kNone);
            }
            slots[entityIndex] = static_cast<std::uint32_t>(components.size());
            owners.push_back(entityIndex);
            components.push_back(std::move(component));
            return components.back();
        }

        void remove(std::uint32_t entityIndex) override
        {
            if (!has(entityIndex)) {
                return;
            }
            // Keep the arrays packed: the last component moves into the gap.
            const std::uint32_t slot = slots[entityIndex];
            const std::uint32_t last = owners.back();
            if (last != entityIndex) {
                components[slot] = std::move(components.back());
                owners[slot]     = last;
                slots[last]      = slot;
            }
            slots[entityIndex] = kNone;
            components.pop_back();
            owners.pop_back();
        }
    };

    // Every component type gets a number the first time it is used; that
    // number is the position of its pool in `pools_`.
    static std::size_t nextTypeId()
    {
        static std::size_t next = 0;
        return next++;
    }

    template <typename T>
    static std::size_t typeId()
    {
        static_assert(std::is_same_v<T, std::decay_t<T>>,
                      "write component types without const, & or *: each<Transform>, not "
                      "each<const Transform&>");
        static const std::size_t id = nextTypeId();
        return id;
    }

    template <typename T>
    Pool<T>& pool()
    {
        const std::size_t id = typeId<T>();
        if (id >= pools_.size()) {
            pools_.resize(id + 1);
        }
        if (!pools_[id]) {
            pools_[id] = std::make_unique<Pool<T>>();
        }
        return static_cast<Pool<T>&>(*pools_[id]);
    }

    template <typename T>
    const Pool<T>* findPool() const
    {
        const std::size_t id = typeId<T>();
        return id < pools_.size() ? static_cast<const Pool<T>*>(pools_[id].get()) : nullptr;
    }

    // Misuse stops the program with an explanation rather than corrupting memory.
    template <typename T>
    [[noreturn]] static void fail(const char* message)
    {
        // GCC and Clang spell the type as its length followed by its name
        // ("9Transform"): skip the digits.
        const char* type = typeid(T).name();
        while (*type >= '0' && *type <= '9') {
            ++type;
        }
        std::fprintf(stderr, "ECS error: %s (component type: %s)\n", message, type);
        std::abort();
    }

    std::vector<std::uint32_t>             generations_; // current generation of every slot
    std::vector<std::uint32_t>             freeSlots_;   // slots of destroyed entities
    std::vector<std::unique_ptr<PoolBase>> pools_;       // indexed by typeId<T>()
};
