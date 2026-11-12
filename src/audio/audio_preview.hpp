#pragma once
#include "audio/audio_clip.hpp"
#include <SDL3/SDL.h>
class AudioPreview {
    SDL_AudioStream* stream_=nullptr;
public:
    std::string path,error;
    ~AudioPreview(){stop();}
    void stop(){if(stream_)SDL_DestroyAudioStream(stream_);stream_=nullptr;path.clear();}
    bool playing(const std::string& asset){
        if(stream_ && SDL_GetAudioStreamQueued(stream_)==0 && SDL_GetAudioStreamAvailable(stream_)==0)stop();
        return stream_ && path==asset;
    }
    void toggle(const AssetManager& assets,const std::string& asset){
        if(playing(asset)){stop();return;}stop();
        try{
            auto samples=decodeAudioClip(assets,asset);
            if(!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(!stream_ || !SDL_PutAudioStreamData(stream_,samples.data(),static_cast<int>(samples.size()*sizeof(float))) || !SDL_FlushAudioStream(stream_) || !SDL_ResumeAudioStreamDevice(stream_))throw std::runtime_error(SDL_GetError());
            path=asset;error.clear();
        }catch(const std::exception& e){stop();error=e.what();}
    }
};
