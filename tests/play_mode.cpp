#include "play_mode.hpp"
#include "level_objects.hpp"
#include "physics.hpp"
#include <cassert>

struct Extra { std::string value; };
int main() {
    World world;
    Entity parent=world.create(), child=world.create();
    world.add(parent,Transform{{0,5,0}});
    world.add(parent,ObjectName{"Original"});
    world.add(parent,RigidBody{});
    world.add(parent,Collider{});
    world.add(parent,Extra{"custom data"});
    world.add(child,Transform{{1,5,0}});
    assert(setParent(world,child,parent));
    PlayMode mode;
    Physics physics;
    assert(!mode.active());
    assert(mode.play(world));
    assert(!mode.play(world));
    physics.update(world,0.1f);
    assert(world.get<Transform>(parent).position.y<5);
    mode.pause(); assert(!mode.playing() && mode.active());
    const float paused=world.get<Transform>(parent).position.y;
    physics.update(world,0);
    assert(world.get<Transform>(parent).position.y==paused);
    mode.pause(); assert(mode.playing());
    Entity copy=duplicateLevelObject(world,parent);
    assert(world.get<ObjectName>(copy).value=="Original (Copy)");
    world.get<Collider>(copy).friction=0.9f;
    assert(world.get<Collider>(parent).friction==0.5f);
    deleteLevelObject(world,parent);
    assert(getParent(world,child)==Entity{});
    world.get<Transform>(child).position.x=99;
    world.remove<Parent>(child);
    Entity temporary=world.create();world.add(temporary,Extra{"temporary"});
    assert(mode.stop(world));
    physics.reset();physics.update(world,0);
    assert(!mode.active());
    assert(world.alive(parent));
    assert(!world.alive(copy));
    assert(!world.alive(temporary));
    assert(world.get<Transform>(parent).position.y==5);
    assert(world.get<Transform>(child).position.x==1);
    assert(getParent(world,child)==parent);
    assert(world.get<Extra>(parent).value=="custom data");
    assert(world.get<RigidBody>(parent).linearVelocity.y==0);
    assert(!mode.stop(world));
    assert(mode.play(world));mode.pause();assert(mode.stop(world));
}
