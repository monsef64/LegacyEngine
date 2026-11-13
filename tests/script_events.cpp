#include "scripting/lua_console.hpp"
#include <cassert>
int main(){
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

}
