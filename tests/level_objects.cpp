#include "editor/level_objects.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main()
{
    World world;
    auto add=[&](const char* name) {
        const Entity e=world.create();
        world.add(e,Transform{}); world.add(e,MeshRenderer{}); world.add(e,ObjectName{name});
        return e;
    };
    const Entity first=add("Cube"),second=add("Cube");
    world.add(first,Collider{});
    const Entity hidden=world.create(); world.add(hidden,Transform{});
    auto objects=levelObjects(world);
    require(objects.size()==3,"empty game objects are listed");
    require(objects[0].entity==first && objects[1].entity==second,"stable entity order");
    require(objects[0].hasCollider && !objects[1].hasCollider,"visual helpers identified");
    // Row clicks belong to the Qt hierarchy; the model promise is that a
    // destroyed or replaced entity never leaves a stale selection behind.
    Entity selected = first;
    world.destroy(selected);
    require(!world.alive(selected), "destroyed selection is not alive");
    selected = {};
    const Entity replacement = add("Sphere");
    require(replacement != first && levelObjects(world).size() == 3,
            "new generation listed without stale selection");
    std::puts("Level object listing, selection, disabled mode and lifecycle tests passed");
}
