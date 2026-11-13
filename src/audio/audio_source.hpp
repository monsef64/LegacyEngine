#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
struct AudioListener { bool enabled=true; void pause(){enabled=false;} void start(){enabled=true;} };
struct AudioSource {
    std::string clip;
    float volume=1,pitch=1,pan=0,distance=50,blend=0;
    bool looping=false,muted=false,playOnStart=false;
    enum class State {Stopped,Playing,Paused};
    State state=State::Stopped;
    unsigned restart=0;
    float fade=1,fadeTarget=1,fadeSeconds=0;
    bool stopAfterFade=false;
    std::vector<std::string> shots;
    void play(){state=State::Playing;++restart;fade=1;fadeSeconds=0;stopAfterFade=false;}
    void stop(){state=State::Stopped;++restart;fade=1;fadeSeconds=0;shots.clear();}
    void pause(){if(state==State::Playing)state=State::Paused;}
    void resume(){if(state==State::Paused)state=State::Playing;}
    float setVolume(float v){return volume=std::clamp(v,0.0f,1.0f);}
    float setPitch(float v){return pitch=std::clamp(v,.1f,4.0f);}
    float setPan(float v){return pan=std::clamp(v,-1.0f,1.0f);}
    float setDistance(float v){return distance=std::clamp(v,0.0f,10000.0f);}
    float spatialBlend(float v){return blend=std::clamp(v,0.0f,1.0f);}
    bool setLooping(bool v){return looping=v;}
    bool isPlaying()const{return state==State::Playing;}
    void mute(){muted=true;}void unmute(){muted=false;}
    void fadeIn(float seconds){play();fade=0;fadeTarget=1;fadeSeconds=std::max(0.0f,seconds);if(!fadeSeconds)fade=1;}
    void fadeOut(float seconds){fadeTarget=0;fadeSeconds=std::max(0.0f,seconds);stopAfterFade=true;if(!fadeSeconds)stop();}
    void advanceFade(float seconds){
        if(state!=State::Playing || fadeSeconds<=0)return;
        const float step=std::min(std::max(0.0f,seconds),fadeSeconds);
        fade+=(fadeTarget-fade)*step/fadeSeconds;fadeSeconds-=step;
        if(fadeSeconds<=0){fade=fadeTarget;if(stopAfterFade)stop();}
    }
    void playOneShot(const std::string& path){if(shots.size()<32)shots.push_back(path);}
};
