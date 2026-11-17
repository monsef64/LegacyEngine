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
    ui::createContext();
    auto& io=ui::io();
    io.DisplaySize=ui::Vec2(1280,720); io.DeltaTime=1.0f/60;

    Entity selected;
    auto frame=[&](bool enabled=true) {
        ui::newFrame(); drawLevelObjects(world,selected,enabled); ui::render();
    };
    auto click=[&](float y,bool enabled=true) {
        ui::injectMousePos(130,y); frame(enabled);
        ui::injectMouseButton(0,true); frame(enabled);
        ui::injectMouseButton(0,false); frame(enabled);
    };
    frame(); frame();
    float rowY=0;
    for (float y=430;y<540 && selected==Entity{};y+=5) { click(y); if (selected!=Entity{}) rowY=y; }
    require(selected==first || selected==second,"clicking a list row selects an entity");
    selected={};
    click(rowY,false);
    require(selected==Entity{},"spawn mode prevents list selection");
    selected=second;
    world.destroy(second);
    frame();
    require(selected==Entity{},"destroyed selection cleared");
    const Entity replacement=add("Sphere");
    require(replacement!=second && levelObjects(world).size()==3,"new generation listed without stale selection");
    ui::destroyContext();
    std::puts("Level object listing, selection, disabled mode and lifecycle tests passed");
}
