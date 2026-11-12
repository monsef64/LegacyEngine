#include "scripting/lua_console.hpp"
#include "scripting/lua_definitions.hpp"
#include "physics/physics.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
static bool contains(const LuaConsole& c,const char* text){for(const auto& l:c.lines())if(l.text.find(text)!=std::string::npos)return true;return false;}
int main(){
    World world;LuaConsole console;console.bindWorld(world,MeshHandle{1},MeshHandle{2},MeshHandle{3});
    require(console.execute(R"(
        local obj=createGameObject('typed')
        assert(obj.getComponent(Transform)~=nil)
        obj:getComponent(Transform).position.x=12
        assert(getComponent(obj,'Transform').position.x==12)
        assert(obj.getComponent('RigidBody')==nil)
        obj.addComponent(RigidBody).mass=7
        assert(obj:getComponent(RigidBody).mass==7)
        obj.getComponent(ObjectName).value='typed renamed'
        assert(obj.name=='typed renamed')
        assert(#findGameObjectsByComponent(Transform)==1)
        assert(BoxCollider==Collider)
        obj:addComponent(BoxCollider).friction=0.75
        assert(getComponent(obj,Collider).friction==0.75)
        obj.removeComponent(RigidBody)
        assert(obj:getComponent(RigidBody)==nil)
        staleRead=obj.getComponent
        destroyGameObject(obj)
    )"),"typed component tokens and bound dot/colon methods");
    require(!console.execute("staleRead(Transform)"),"stale bound method rejected");
    require(!console.execute("getComponent(createGameObject('bad'), {})"),"invalid token rejected");
    console.reset();
    require(console.execute(R"(
        starts=0; updates=0; fixed=0; destroyed=0
        function onStart() starts=starts+1 end
        function onUpdate(dt) updates=updates+1; assert(dt==0.125) end
        function onFixedUpdate(dt) fixed=fixed+1; assert(math.abs(dt-1/60)<0.0001) end
        function onDestroy() print('game stopped') end
        ball=createGameObject('ball')
        assert(ball.name=='ball' and getComponent(ball,'Transform')~=nil)
        assert(getComponent(ball,'RigidBody')==nil)
        body=addComponent(ball,'RigidBody'); body.useGravity=false; body.mass=3
        render=addComponent(ball,'MeshRenderer');render.mesh='Sphere';render.color={x=0,y=1,z=0}
        collider=addComponent(ball,'Collider');collider.shape='Sphere';collider.radius=0.5
        transform=getComponent(ball,'Transform');transform.position={x=1,y=2,z=3}
        position=transform.position
        ball.onStart=function() print('ball started') end
        ball.onUpdate=function(dt) position.x=position.x+dt end
        ball.onFixedUpdate=function(dt) body.linearVelocity.y=dt end
        ball.onDestroy=function() destroyed=destroyed+1; print('ball destroyed') end
        assert(findGameObjectByName('ball')==ball)
        assert(findGameObjectByName('missing')==nil)
        assert(#findGameObjectsByComponent('RigidBody')==1)
    )"),"scene APIs execute");
    require(contains(console,"ball started")==false,"object start deferred until tick");
    console.update(0.125f);console.fixedUpdate(1.0f/60);
    require(contains(console,"ball started"),"object onStart invoked");
    Entity ball;world.each<ObjectName>([&](Entity e,ObjectName& n){if(n.value=="ball")ball=e;});
    require(world.alive(ball) && world.get<Transform>(ball).position.x==1.125f,"vector proxy writes live transform");
    require(world.get<MeshRenderer>(ball).mesh.id==2 && world.get<Collider>(ball).shape==ColliderShape::Sphere,"render and collider exposed");
    // Pool growth cannot invalidate saved Lua proxies.
    for(int i=0;i<300;++i){auto e=world.create();world.add(e,Transform{});}
    require(console.execute("assert(starts==1 and updates==1 and fixed==1); position.z=7; removeComponent(ball,'RigidBody'); assert(getComponent(ball,'RigidBody')==nil)"),"persistent handles and queries");
    require(world.get<Transform>(ball).position.z==7,"proxy survives ECS reallocation");
    require(!console.execute("body.mass=9"),"removed component proxy rejected");
    require(console.execute("destroyGameObject(ball); assert(destroyed==1); assert(findGameObjectByName('ball')==nil)"),"explicit destroy calls lifecycle once");
    require(!world.alive(ball),"object destroyed in ECS");
    require(!console.execute("position.x=4"),"destroyed entity proxy rejected");
    auto replacement=world.create();world.add(replacement,Transform{});
    require(!console.execute("getComponent(ball,'Transform')"),"stale generation cannot access recycled entity");
    require(console.execute("external=findGameObjectByName('external')"),"missing lookup harmless");
    const auto e=world.create();world.add(e,Transform{});world.add(e,ObjectName{"external"});
    require(console.execute("external=findGameObjectByName('external'); external.onDestroy=function() print('external removed') end"),"attach callbacks to existing object");
    world.destroy(e);console.update(0.125f);
    require(contains(console,"external removed"),"external destruction detected");
    require(console.execute("function onUpdate(dt) while true do end end"),"runaway callback installed");
    console.update(0.1f);require(contains(console,"instruction limit"),"callback budget enforced");
    console.reset();require(contains(console,"game stopped"),"shutdown callback runs even after callback error");
    require(console.execute("owned=createGameObject('owned')"),"fresh world binding after reset");
    console.reset();
    bool found=false;world.each<ObjectName>([&](Entity,ObjectName& n){if(n.value=="owned")found=true;});
    require(!found,"reset destroys script-owned objects");
    // Fixed callbacks occur once before each actual physics step, including component changes.
    Physics physics;int steps=0;
    physics.update(world,3.0f*(1.0f/60),[&](float dt){require(dt>0.016f && dt<0.017f,"fixed delta seconds");++steps;});
    require(steps==3,"fixed callback aligned to solver steps");
    Entity fixedObject;
    physics.update(world,1.0f/60,[&](float){
        fixedObject=world.create();world.add(fixedObject,Transform{{0,5,0}});
        RigidBody rb;rb.useGravity=false;rb.linearVelocity={6,0,0};world.add(fixedObject,rb);
        world.add(fixedObject,Collider{});
    });
    physics.update(world,1.0f/60);
    require(world.get<Transform>(fixedObject).position.x>0.09f,"fixed callback ECS additions participate in the same solver step");
    // Object destruction may create another object; iteration remains valid.
    require(console.execute(R"(
        victim=createGameObject('victim')
        victim.onDestroy=function() createGameObject('replacement') end
        destroyGameObject(victim)
        assert(findGameObjectByName('replacement')~=nil)
    )"),"reentrant destruction and creation safe");
    require(luaCompletion("createG")=="createGameObject" && luaCompletion("findGameObject")=="findGameObject", "completion definitions match API names");
    require(console.execute(R"(
        parent=createGameObject('parent')
        child=createGameObject('child')
        getComponent(child,'Transform').position.x=1
        setParent(child,parent)
        assert(getParent(child)==parent and child.parent==parent)
        getComponent(parent,'Transform').position.x=4
    )"),"Lua parenting API available");
    console.update(0);
    require(console.execute("assert(getComponent(child,'Transform').position.x==5); child.parent=nil; assert(getParent(child)==nil)"),"Lua hierarchy propagation and detach");
    std::puts("Lua scene lifecycle, live components, stale handles, cleanup and fixed-step tests passed");
}
