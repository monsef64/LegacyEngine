#include "scripting/lua_console.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool contains(const LuaConsole& console,LuaConsole::Kind kind,const char* text)
{
    for (const auto& line:console.lines()) if (line.kind==kind && line.text.find(text)!=std::string::npos) return true;
    return false;
}
int main()
{
    using Kind=LuaConsole::Kind;
    LuaConsole console;
    require(console.ready(),"runtime starts");
    require(console.lines().empty(),"runtime starts without ready banner");
    console.clear();
    require(console.execute("local value = 42"),"silent script succeeds");
    require(console.lines().empty(),"silent scripts produce no execution banners");
    require(console.execute("print('Hello', 42, true, nil)"),"print succeeds");
    require(console.lines().size()==1 && console.lines().front().text=="lua > Hello\t42\ttrue\tnil",
            "output has Lua prompt and no execution banners");
    require(contains(console,Kind::Output,"Hello\t42\ttrue\tnil"),"print routes values to transcript");
    require(console.execute("print('100% %s %n'); print()"),"format strings treated as text");
    require(contains(console,Kind::Output,"100% %s %n"),"format string preserved");
    require(console.execute("counter = (counter or 0) + 1"),"state assigned");
    require(console.execute("print(counter); print(math.sqrt(81), string.upper('hi'), utf8.len('hello'))"),"standard libraries work");
    require(contains(console,Kind::Output,"9.0\tHI\t5"),"library output captured");
    require(!console.execute("local ="),"syntax errors reported");
    require(contains(console,Kind::Error,"editor"),"syntax error includes source");
    require(!console.execute("local function fail() error('broken') end; fail()"),"runtime errors reported");
    require(contains(console,Kind::Error,"broken") && contains(console,Kind::Error,"stack traceback"),"runtime traceback captured");
    require(!console.execute("error({})"),"non-string errors handled");
    require(console.execute("print('recovered')"),"errors do not poison future executions");
    require(console.execute("debug.log('Log', 42); debug.warning('Careful'); debug.error('Failure'); print('Still running')"),"logging does not throw");
    require(contains(console,Kind::Warning,"Careful") && contains(console,Kind::Error,"Failure") && contains(console,Kind::Output,"Still running"),"debug severities routed correctly");
    require(console.execute("assert(io == nil and os == nil and package == nil and require == nil and type(debug) == 'table' and debug.getregistry == nil and debug.sethook == nil and coroutine == nil and load == nil and loadfile == nil and dofile == nil and pcall == nil and xpcall == nil and setmetatable == nil)"),"host access and budget bypass APIs omitted");
    require(!console.execute("while true do end"),"infinite loop interrupted");
    require(contains(console,Kind::Error,"instruction limit"),"execution limit reported");
    require(!console.execute("local x=string.rep('x', 100000000)"),"memory allocation bounded");
    require(contains(console,Kind::Error,"memory"),"out of memory error captured");
    require(console.execute("print('after limits')"),"runtime recovers from limits");
    require(console.execute("print(string.rep('x',20000))"),"long output bounded");
    for (const auto& line:console.lines()) require(line.text.size()<LuaConsole::maxLineBytes+32,"line length bounded");
    require(console.execute("for i=1,2000 do print(i) end"),"many prints succeed");
    require(console.lines().size()==LuaConsole::maxLines,"console history bounded");
    require(!console.execute(std::string(LuaConsole::maxSourceBytes+1,' ')),"oversized source rejected");
    console.clear();
    require(console.lines().empty(),"clear empties transcript");
    require(console.execute("print(counter)"),"clear preserves runtime globals");
    require(contains(console,Kind::Output,"1"),"state persists after clear");
    console.reset();
    require(console.execute("assert(counter == nil); print('reset')"),"reset clears globals");
    for (int i=0;i<100;++i) require(console.execute("print(123)"),"repeated execution has balanced stack");
    std::puts("Lua output, errors, persistence, limits and recovery tests passed");
}
