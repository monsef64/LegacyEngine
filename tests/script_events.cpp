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
}
