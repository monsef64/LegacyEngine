#include "scripting/lua_console.hpp"
#include "prefabs/prefab.hpp"
#include <cassert>
int main(){
    {
        World tree;LuaConsole c;c.bindWorld(tree);
        for(const char* removal:{"destroyGameObject(parent)","destroy(parent)","parent:destroy()"}) {
            assert(c.execute(R"(
                parent=GameObject.new("Delete parent")
                child=GameObject.new("Delete child");child:setParent(parent)
                grandchild=GameObject.new("Delete grandchild");grandchild:setParent(child)
                child:setActive(false)
                survivor=GameObject.new("Keep child");survivor:setParent(parent);survivor:setParent(nil)
            )"));
            assert(tree.entities().size()==4);assert(c.execute(removal));assert(tree.entities().size()==1);
            assert(c.execute("assert(survivor:getName()=='Keep child');destroy(survivor)"));
            assert(tree.entities().empty());
        }
    }
    LuaConsole::clearEvents();
    LuaConsole first,second;
    assert(first.execute("count=0; token=on('ping',function(value) count=count+value end)"));
    assert(second.execute("received=''; on('ping',function(value) received=tostring(value) end); assert(emit('ping',3))"));
    assert(first.execute("assert(count==0)"));LuaConsole::dispatchEvents();
    assert(first.execute("assert(count==3); assert(off(token));assert(not off(token))"));
    assert(second.execute("assert(tonumber(received)==3); emit('ping',4)"));LuaConsole::dispatchEvents();
    assert(first.execute("assert(count==3)"));assert(second.execute("assert(tonumber(received)==4)"));
    assert(!first.execute("emit('bad',{})"));
    assert(!first.execute("on('bad',123)"));
    assert(first.execute("bad=0; on('broken',function() bad=bad+1; error('listener failed') end);emit('broken')"));
    LuaConsole::dispatchEvents();
    assert(first.execute("emit('broken')"));LuaConsole::dispatchEvents();assert(first.execute("assert(bad==1)"));
    assert(first.execute("chain=0; on('chain',function() chain=chain+1; emit('chain') end); emit('chain')"));
    LuaConsole::dispatchEvents();assert(first.execute("assert(chain==1)"));LuaConsole::dispatchEvents();assert(first.execute("assert(chain==2)"));LuaConsole::clearEvents();
    assert(first.execute("loaded='';function onLevelLoaded(name) loaded=name end; function onLevelFailedToLoad(name,reason) failed=name..':'..reason end"));
    assert(LuaConsole::postEvent("onLevelLoaded",{std::string("Main")}));
    assert(LuaConsole::postEvent("onLevelFailedToLoad",{std::string("Missing"),std::string("not found")}));
    LuaConsole::dispatchEvents();assert(first.execute("assert(loaded=='Main' and failed=='Missing:not found')"));
    first.shutdown();assert(second.execute("emit('ping',9)"));LuaConsole::dispatchEvents();assert(first.execute("assert(count==3)"));
    first.reset();assert(first.execute("calls=0;on('new',function()calls=calls+1 end)"));
    assert(LuaConsole::postEvent("new"));LuaConsole::dispatchEvents();assert(first.execute("assert(calls==1)"));
    assert(!first.execute("error('file failure')","Scripts/Error.ls"));
    assert(first.lines().back().text.find("Scripts/Error.ls:1")!=std::string::npos);
    World world;first.bindWorld(world);
    assert(!first.execute("function onStart() error('start failed') end","Scripts/Start.ls"));
    assert(first.lines().back().text.find("Scripts/Start.ls")!=std::string::npos);
    LuaConsole::clearEvents();for(int i=0;i<256;++i)assert(LuaConsole::postEvent("bounded"));assert(!LuaConsole::postEvent("overflow"));LuaConsole::clearEvents();
    {
        World w;auto e=w.create();w.add(e,Transform{});w.add(e,AudioSource{});
        LuaConsole c;c.bindWorld(w);assert(c.bindObject(e));
        assert(c.execute(R"(
            function onStart()
                local t=self:getComponent(Transform)
                t.position.x=1
                waitForSeconds(0.5)
                t.position.x=2
                local child=GameObject.new("After wait")
                child:setPosition(1,2,3)
                local audio=self:getComponent(AudioSource)
                audio:play()
                wait(audio.onAudioFinished)
                t.position.x=3
            end
            function onMouseDown() self:getComponent(Transform).position.y=1 end
            self.onMouseEnter=function() self:getComponent(Transform).position.z=1 end
            function onAudioPaused() self:getComponent(Transform).position.y=2 end
        )","Scripts/Wait.ls"));
        assert(w.get<Transform>(e).position.x==1);c.update(.25f);assert(w.get<Transform>(e).position.x==1);
        setObjectActive(w,e,false);c.update(10);assert(w.get<Transform>(e).position.x==1);
        c.objectEvent(e,"onMouseDown");assert(w.get<Transform>(e).position.y==0);
        setObjectActive(w,e,true);c.update(.25f);assert(w.get<Transform>(e).position.x==2);
        assert(w.entities().size()==2);c.objectEvent(e,"onMouseDown");assert(w.get<Transform>(e).position.y==1);
        c.objectEvent(e,"onMouseEnter");assert(w.get<Transform>(e).position.z==1);
        auto& audio=w.get<AudioSource>(e);audio.pause();assert(audio.eventSequence[2]==1);
        c.objectEvent(e,"onAudioPaused");assert(w.get<Transform>(e).position.y==2);
        audio.resume();assert(audio.eventSequence[3]==1);audio.finish();c.update(0);
        assert(w.get<Transform>(e).position.x==3 && audio.eventSequence[1]==1);
        c.shutdown();
    }
    {
        World w;auto e=w.create();w.add(e,Transform{});LuaConsole c;c.bindWorld(w);assert(c.bindObject(e));
        assert(!c.execute("waitForSeconds(1)"));
        assert(c.execute("function onStart() waitForSeconds(0); while true do end end","Scripts/Budget.ls"));
        c.update(.1f);assert(c.lines().back().text.find("instruction limit")!=std::string::npos);
        c.reset();assert(c.bindObject(e));
        assert(c.execute("function onStart() waitForSeconds(1); self:setPosition(99,0,0) end"));
        c.shutdown();c.update(2);assert(w.get<Transform>(e).position.x==0);
    }

    {
        World w;auto entity=w.create();w.add(entity,Transform{});w.add(entity,ObjectName{"Toggle"});
        LuaConsole c;c.bindWorld(w);assert(c.bindObject(entity));
        assert(c.execute("self:setActive(false); assert(not self:isActive()); function onUpdate(dt) self:translate(dt,0,0) end"));
        c.update(1);assert(w.get<Transform>(entity).position.x==0);
        LuaConsole controller;controller.bindWorld(w);
        assert(controller.execute("GameObject.find('Toggle'):setActive(true)"));
        c.update(1);assert(w.get<Transform>(entity).position.x==1);
    }

    {
        World w;auto e=w.create();w.add(e,Transform{});w.add(e,MeshRenderer{});LuaConsole c;c.bindWorld(w);assert(c.bindObject(e));
        assert(c.execute(R"(
            function onMouseEnter() getComponent(MeshRenderer):setMaterialColor(1,0.6,0.3) end
            function onMouseExit() getComponent(MeshRenderer):setMaterialColor(0.1,0.2,0.3) end
            clone=spawn(self,{4,5,6},{0,90,0});destroy(clone,0.5)
        )"));
        c.objectEvent(e,"onMouseEnter");assert(w.get<MeshRenderer>(e).materialValues.color.x==1);
        c.objectEvent(e,"onMouseExit");assert(w.get<MeshRenderer>(e).materialValues.color.x==.1f);
        assert(w.entities().size()==2);Prefabs::updateDestroyTimers(w,.5f);assert(w.entities().size()==1);
        assert(!c.execute("destroy(self,-1)"));
    }

    {
        AssetManager assets;assets.root=std::filesystem::temp_directory_path()/"legacy-lua-prefab-test";
        std::filesystem::remove_all(assets.root);std::filesystem::create_directories(assets.root);
        World source;auto root=source.create();source.add(root,Transform{});source.add(root,ObjectName{"Asset root"});
        const ProjectIO::Meshes meshes{};Prefabs::save(assets,"Root.prefab",source,root,meshes);
        World w;LuaConsole c;c.setAssetRoot(assets.root);c.bindWorld(w);
        assert(c.execute("instance=spawn('Root.prefab',{1,2,3},{0,90,0});assert(instance:getPosition().x==1);destroy(instance)"));
        assert(w.entities().empty());assert(!c.execute("spawn('../bad.prefab',{0,0,0},{0,0,0})"));
        std::filesystem::remove_all(assets.root);
    }

}
