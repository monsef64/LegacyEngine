#include "core/ecs.hpp"
#include <cassert>
#include <type_traits>
#include <vector>

struct Position { int x; };
struct Visible { bool value; };
struct Missing {};

int main() {
    World world;
    const World& readOnly = world;
    int visits = 0;
    readOnly.eachReadOnly<Missing>([&](Entity, const Missing&) { ++visits; });
    assert(visits == 0);
    const auto first = world.create(), second = world.create(), third = world.create();
    world.add(first, Position{1}); world.add(first, Visible{true});
    world.add(second, Position{2});
    world.add(third, Position{3}); world.add(third, Visible{false});
    std::vector<Entity> expected, actual;
    world.each<Position, Visible>([&](Entity e, const Position&, const Visible&) { expected.push_back(e); });
    readOnly.eachReadOnly<Position, Visible>([&](Entity e, auto& p, auto& v) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(p)>>);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(v)>>);
        actual.push_back(e);
    });
    assert(actual == expected && actual.size() == 2);
    readOnly.eachReadOnly<Position>([&](Entity, const Position&) { ++visits; });
    assert(visits == 3);
    readOnly.eachReadOnly<Position, Missing>([&](Entity, const Position&, const Missing&) { assert(false); });
    world.destroy(first);
    const auto reused = world.create(); assert(reused.index == first.index && reused != first);
    world.add(reused, Position{4}); world.add(reused, Visible{true});
    readOnly.eachReadOnly<Position, Visible>([&](Entity e, const Position& p, const Visible&) {
        assert(e != first && (p.x == 3 || p.x == 4));
    });
    auto snapshot = world.snapshot(); world.restore(*snapshot);
    visits = 0;
    readOnly.eachReadOnly<Position>([&](Entity, const Position&) { ++visits; }); assert(visits == 3);
    // The existing mutation-safe API still snapshots IDs and skips removed entities.
    bool created = false;
    world.each<Position>([&](Entity e, Position&) {
        if (!created) { created = true; auto extra = world.create(); world.add(extra, Position{5}); }
        world.remove<Position>(e);
    });
    visits = 0;
    readOnly.eachReadOnly<Position>([&](Entity, const Position& p) { ++visits; assert(p.x == 5); });
    assert(visits == 1);
}
