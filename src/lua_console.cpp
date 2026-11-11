#include "lua_console.hpp"
#include "lua_scene.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <algorithm>
#include <cstdlib>

void* LuaConsole::allocate(void* user, void* pointer, std::size_t oldSize, std::size_t newSize)
{
    auto& self=*static_cast<LuaConsole*>(user);
    if (!pointer) oldSize=0; // Lua uses oldSize as a type tag for new allocations.
    if (newSize==0) {
        std::free(pointer);
        self.allocated_-=oldSize;
        return nullptr;
    }
    if (newSize>memoryLimit || self.allocated_-oldSize>memoryLimit-newSize) return nullptr;
    void* result=std::realloc(pointer,newSize);
    if (result) self.allocated_=self.allocated_-oldSize+newSize;
    return result;
}

LuaConsole::LuaConsole() { reset(); }
LuaConsole::~LuaConsole() { shutdown(); if (state_) lua_close(state_); }

void LuaConsole::append(Kind kind, std::string_view text)
{
    if (lines_.size()>=maxLines) lines_.pop_front();
    // Bound the host transcript as well as Lua's own memory.
    std::string message = kind == Kind::Output ? "lua > " : "";
    message.append(text.substr(0,maxLineBytes));
    if (text.size()>maxLineBytes) message += " [truncated]";
    lines_.push_back({kind,std::move(message)});
}

void LuaConsole::reset()
{
    shutdown();
    if (state_) lua_close(state_);
    state_=nullptr;
    allocated_=0;
    state_=lua_newstate(allocate,this);
    if (!state_) { append(Kind::Error,"Unable to create Lua runtime"); return; }
    *static_cast<LuaConsole**>(lua_getextraspace(state_))=this;
    // Snippets have basic computation libraries, but no host filesystem, process,
    // native module loading or debug access. No coroutines or catchable pcall:
    // scripts must not swallow the instruction-limit error and run forever.
    luaL_requiref(state_,"_G",luaopen_base,1); lua_pop(state_,1);
    luaL_requiref(state_,LUA_TABLIBNAME,luaopen_table,1); lua_pop(state_,1);
    luaL_requiref(state_,LUA_STRLIBNAME,luaopen_string,1); lua_pop(state_,1);
    luaL_requiref(state_,LUA_MATHLIBNAME,luaopen_math,1); lua_pop(state_,1);
    luaL_requiref(state_,LUA_UTF8LIBNAME,luaopen_utf8,1); lua_pop(state_,1);
    for (const char* name : {"dofile","loadfile","load","collectgarbage","pcall","xpcall","warn","setmetatable","getmetatable"}) {
        lua_pushnil(state_); lua_setglobal(state_,name);
    }
    lua_pushlightuserdata(state_,this);
    lua_pushcclosure(state_,print,1); lua_setglobal(state_,"print");
    installScene();
    append(Kind::Info,"Lua 5.4 ready. print(...) writes here.");
}

int LuaConsole::print(lua_State* state)
{
    auto* self=static_cast<LuaConsole*>(lua_touserdata(state,lua_upvalueindex(1)));
    // Do all Lua calls before constructing C++ objects: a Lua error longjmps.
    const int count=lua_gettop(state);
    luaL_checkstack(state,count+8,"Too many print arguments");
    for (int i=1;i<=count;++i) luaL_tolstring(state,i,nullptr);
    std::string output;
    for (int i=0;i<count && output.size()<maxLineBytes;++i) {
        if (i) output+='\t';
        std::size_t length=0;
        const char* text=lua_tolstring(state,count+1+i,&length);
        const std::size_t take=std::min(length,maxLineBytes-output.size());
        for (std::size_t j=0;j<take;++j) output += text[j]=='\0' ? '?' : text[j];
        if (length>take) { output+=" [truncated]"; break; }
    }
    self->append(Kind::Output,output);
    return 0;
}

int LuaConsole::traceback(lua_State* state)
{
    const char* message=lua_type(state,1)==LUA_TSTRING ? lua_tostring(state,1) : "(non-string Lua error)";
    luaL_traceback(state,state,message,1);
    return 1;
}

void LuaConsole::instructionHook(lua_State* state,lua_Debug*)
{
    auto* self=*static_cast<LuaConsole**>(lua_getextraspace(state));
    self->remainingInstructions_-=1000;
    if (self->remainingInstructions_<=0) luaL_error(state,"Execution stopped: instruction limit exceeded");
}

bool LuaConsole::execute(std::string_view source)
{
    if (!state_) { append(Kind::Error,"Lua runtime is unavailable"); return false; }
    if (source.size()>maxSourceBytes) { append(Kind::Error,"Script exceeds the 64 KiB editor limit"); return false; }
    lua_settop(state_,0);
    beginBudget();
    lua_pushcfunction(state_,traceback);
    int status=luaL_loadbufferx(state_,source.data(),source.size(),"editor","t");
    if (status==LUA_OK) status=lua_pcall(state_,0,0,1);
    if (status==LUA_OK && scene_) scene_->start();
    endBudget();
    if (status!=LUA_OK) {
        const char* message=lua_tostring(state_,-1);
        append(Kind::Error,message ? message : "Unknown Lua error");
    }
    lua_settop(state_,0);
    lua_gc(state_,LUA_GCCOLLECT,0);
    return status==LUA_OK;
}

void LuaConsole::beginBudget() {
    remainingInstructions_=1000000;
    lua_sethook(state_,instructionHook,LUA_MASKCOUNT,1000);
}
void LuaConsole::endBudget() { lua_sethook(state_,nullptr,0,0); }
void LuaConsole::installScene() {
    if(world_ && state_) scene_=std::make_unique<LuaScene>(state_,*world_,meshes_[0],meshes_[1],meshes_[2],
        [this](std::string_view message){append(Kind::Error,message);});
}
void LuaConsole::bindWorld(World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule) {
    shutdown();world_=&world;meshes_[0]=cube;meshes_[1]=sphere;meshes_[2]=capsule;installScene();
}
void LuaConsole::update(float dt) { if(scene_){beginBudget();scene_->update(dt);endBudget();} }
void LuaConsole::fixedUpdate(float dt) { if(scene_){beginBudget();scene_->fixedUpdate(dt);endBudget();} }
void LuaConsole::shutdown() {
    if(scene_){beginBudget();scene_->stop();endBudget();scene_.reset();}
}
