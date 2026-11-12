#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
struct InputState {
    std::array<bool,SDL_SCANCODE_COUNT> keys{},pressed{},released{};
    std::array<bool,9> mouse{},mousePressed{},mouseReleased{};
    std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> pad{},padPressed{};
    std::array<float,SDL_GAMEPAD_AXIS_COUNT> axes{};
    std::map<std::string,int> buttons;
    float x=0,y=0,dx=0,dy=0,wheelX=0,wheelY=0;
    bool enabled=false,mouseEnabled=false;
    SDL_Gamepad* gamepad=nullptr;
    inline static InputState* current=nullptr;
    InputState(){current=this;}
    ~InputState(){if(gamepad)SDL_CloseGamepad(gamepad);if(current==this)current=nullptr;}
    void clear(){keys.fill(false);pressed.fill(false);released.fill(false);mouse.fill(false);mousePressed.fill(false);mouseReleased.fill(false);pad.fill(false);padPressed.fill(false);axes.fill(0);dx=dy=wheelX=wheelY=0;}
    void beginFrame(){
        pressed.fill(false);released.fill(false);mousePressed.fill(false);mouseReleased.fill(false);padPressed.fill(false);dx=dy=wheelX=wheelY=0;
        if(!enabled){clear();return;}
        if(!SDL_WasInit(SDL_INIT_GAMEPAD))SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        if(gamepad && !SDL_GamepadConnected(gamepad)){SDL_CloseGamepad(gamepad);gamepad=nullptr;pad.fill(false);axes.fill(0);}
        if(!gamepad){int count=0;auto* ids=SDL_GetGamepads(&count);if(count)gamepad=SDL_OpenGamepad(ids[0]);SDL_free(ids);}
        if(gamepad){
            SDL_PumpEvents();SDL_UpdateGamepads();
            for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i){bool down=SDL_GetGamepadButton(gamepad,static_cast<SDL_GamepadButton>(i));padPressed[i]=down&&!pad[i];pad[i]=down;}
            for(int i=0;i<SDL_GAMEPAD_AXIS_COUNT;++i){float v=SDL_GetGamepadAxis(gamepad,static_cast<SDL_GamepadAxis>(i))/32767.0f;axes[i]=std::abs(v)<.15f?0:std::clamp(v,-1.0f,1.0f);}
        }
    }
    void event(const SDL_Event& e){
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){clear();return;}
        if(e.type==SDL_EVENT_KEY_UP || e.type==SDL_EVENT_KEY_DOWN){int key=e.key.scancode;if(key>0 && key<SDL_SCANCODE_COUNT){bool down=e.type==SDL_EVENT_KEY_DOWN;
            if(down && enabled && !e.key.repeat){pressed[key]|=!keys[key];keys[key]=true;}
            if(!down){released[key]|=enabled && keys[key];keys[key]=false;}}
        }
        if(e.type==SDL_EVENT_MOUSE_BUTTON_UP || e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){int button=e.button.button;if(button>0 && button<9){bool down=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
            if(down && enabled && mouseEnabled){mousePressed[button]|=!mouse[button];mouse[button]=true;}if(!down){mouseReleased[button]|=enabled && mouse[button];mouse[button]=false;}}
        }
        if(e.type==SDL_EVENT_MOUSE_MOTION){x=e.motion.x;y=e.motion.y;if(enabled && mouseEnabled){dx+=e.motion.xrel;dy+=e.motion.yrel;}}
        if(e.type==SDL_EVENT_MOUSE_WHEEL && enabled && mouseEnabled){float sign=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;wheelX+=e.wheel.x*sign;wheelY+=e.wheel.y*sign;}
    }
    bool button(const std::string& name,int phase)const{auto it=buttons.find(name);if(!enabled || it==buttons.end() || it->second<=0 || it->second>=SDL_SCANCODE_COUNT)return false;return phase==0?pressed[it->second]:phase==1?keys[it->second]:released[it->second];}
    float axis(const std::string& negative,const std::string& positive)const{return float(button(positive,1))-float(button(negative,1));}
};
