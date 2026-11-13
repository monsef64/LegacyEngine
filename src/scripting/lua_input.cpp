#include "scripting/lua_console.hpp"
#include "input/input.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
void vector(lua_State* L,float x,float y){lua_createtable(L,0,2);lua_pushnumber(L,x);lua_setfield(L,-2,"x");lua_pushnumber(L,y);lua_setfield(L,-2,"y");}
int gamepadButton(lua_State* L){
    if(lua_type(L,1)==LUA_TNUMBER)return static_cast<int>(luaL_checkinteger(L,1));
    const char* name=luaL_checkstring(L,1);
    if(!SDL_strcasecmp(name,"south"))return SDL_GAMEPAD_BUTTON_SOUTH;
    if(!SDL_strcasecmp(name,"east"))return SDL_GAMEPAD_BUTTON_EAST;
    if(!SDL_strcasecmp(name,"west"))return SDL_GAMEPAD_BUTTON_WEST;
    if(!SDL_strcasecmp(name,"north"))return SDL_GAMEPAD_BUTTON_NORTH;
    return SDL_GetGamepadButtonFromString(name);
}
int inputCall(lua_State* L){
    const int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(1)));auto* input=InputState::current;
    if(op>=18){
        if(op==22){lua_pushboolean(L,input && input->window && !SDL_GetWindowRelativeMouseMode(input->window) && SDL_CursorVisible());return 1;}
        if(op==23){lua_pushboolean(L,input && input->window && SDL_GetWindowRelativeMouseMode(input->window));return 1;}
        bool ok=false;if(input){if(op==18 || op==19)ok=input->lockCursor(op==18);else ok=input->showCursor(op==21);}
        lua_pushboolean(L,ok);return 1;
    }
    if(op<=2){int key=lua_type(L,1)==LUA_TNUMBER?static_cast<int>(luaL_checkinteger(L,1)):SDL_GetScancodeFromName(luaL_checkstring(L,1));bool value=false;if(input && input->enabled && key>0 && key<SDL_SCANCODE_COUNT)value=op==0?input->pressed[key]:op==1?input->keys[key]:input->released[key];lua_pushboolean(L,value);return 1;}
    if(op<=5){int button=0;if(lua_type(L,1)==LUA_TSTRING){const char* name=luaL_checkstring(L,1);button=!SDL_strcasecmp(name,"left")?1:!SDL_strcasecmp(name,"middle")?2:!SDL_strcasecmp(name,"right")?3:0;}else button=static_cast<int>(luaL_checkinteger(L,1));bool value=false;if(input && input->enabled && button>0 && button<9)value=op==3?input->mousePressed[button]:op==4?input->mouse[button]:input->mouseReleased[button];lua_pushboolean(L,value);return 1;}
    if(op<=8){float x=0,y=0;if(input && input->enabled){x=op==6?input->x:op==7?input->dx:input->wheelX;y=op==6?input->y:op==7?input->dy:input->wheelY;}vector(L,x,y);return 1;}
    if(op==9 || op==10){const char* a=luaL_checkstring(L,1);const char* b=luaL_checkstring(L,2);float x=input?input->axis(a,b):0;if(op==9){lua_pushnumber(L,x);return 1;}const char* c=luaL_checkstring(L,3);const char* d=luaL_checkstring(L,4);float y=input?input->axis(c,d):0;float length=std::sqrt(x*x+y*y);if(length>1){x/=length;y/=length;}vector(L,x,y);return 1;}
    if(op==11 || op==12){int button=gamepadButton(L);lua_pushboolean(L,input && input->enabled && button>=0 && button<SDL_GAMEPAD_BUTTON_COUNT && (op==11?input->padPressed[button]:input->pad[button]));return 1;}
    if(op==13){int axis=lua_type(L,1)==LUA_TNUMBER?static_cast<int>(luaL_checkinteger(L,1)):SDL_GetGamepadAxisFromString(luaL_checkstring(L,1));lua_pushnumber(L,input && input->enabled && axis>=0 && axis<SDL_GAMEPAD_AXIS_COUNT?input->axes[axis]:0);return 1;}
    if(op==14){const char* stick=luaL_optstring(L,1,"left");int axis=!SDL_strcasecmp(stick,"right")?2:0;vector(L,input && input->enabled?input->axes[axis]:0,input && input->enabled?input->axes[axis+1]:0);return 1;}
    const char* name=luaL_checkstring(L,1);lua_pushboolean(L,input && input->button(name,op-15));return 1;
}
}
void LuaConsole::installInput(){
    const char* names[]{"isKeyPressed","isKeyDown","isKeyReleased","isMouseButtonPressed","isMouseButtonDown","isMouseButtonReleased","getMousePosition","getMouseDelta","getMouseWheelDelta","getInputAxis","getInputVector","isGamepadButtonPressed","isGamepadButtonDown","getGamepadAxis","getGamepadStick","isButtonPressed","isButtonDown","isButtonReleased","lockCursor","unlockCursor","hideCursor","showCursor","isCursorVisible","isCursorLocked"};
    for(int i=0;i<24;++i){lua_pushinteger(state_,i);lua_pushcclosure(state_,inputCall,1);lua_setglobal(state_,names[i]);}
}
