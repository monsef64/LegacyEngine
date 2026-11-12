#pragma once
#include "assets.hpp"
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
            auto file=assets.resolve(asset);
            if(AssetManager::extension(file)!=".wav" || std::filesystem::file_size(file)>32*1024*1024)throw std::runtime_error("Preview requires WAV under 32 MiB");
            if(!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec spec;Uint8* data=nullptr;Uint32 length=0;
            if(!SDL_LoadWAV(file.string().c_str(),&spec,&data,&length))throw std::runtime_error(SDL_GetError());
            stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            const bool queued=stream_ && SDL_PutAudioStreamData(stream_,data,static_cast<int>(length));SDL_free(data);
            if(!queued || !SDL_FlushAudioStream(stream_) || !SDL_ResumeAudioStreamDevice(stream_))throw std::runtime_error(SDL_GetError());
            path=asset;error.clear();
        }catch(const std::exception& e){stop();error=e.what();}
    }
};
