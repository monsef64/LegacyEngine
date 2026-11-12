#pragma once
#include "assets.hpp"
#include <SDL3/SDL.h>
#include <map>
#include <memory>

// Main-thread PCM mixer. No ECS pointers escape into SDL's audio thread.
class AudioSystem {
    struct Voice {Entity entity;std::string clip;double cursor=0;unsigned restart=0;bool shot=false;};
    std::vector<Voice> voices;
    std::map<std::string,std::vector<float>> cache;
    SDL_AudioStream* stream=nullptr;
    std::filesystem::path root;
    std::size_t cachedBytes=0;
    const std::vector<float>& load(const AssetManager& assets,const std::string& path) {
        auto found=cache.find(path);if(found!=cache.end())return found->second;
        auto file=assets.resolve(path);
        if(AssetManager::extension(file)!=".wav" || std::filesystem::file_size(file)>32*1024*1024)throw std::runtime_error("AudioClip requires a WAV file under 32 MiB");
        SDL_AudioSpec spec;Uint8* data=nullptr;Uint32 length=0;
        if(!SDL_LoadWAV(file.string().c_str(),&spec,&data,&length))throw std::runtime_error(SDL_GetError());
        std::unique_ptr<Uint8,decltype(&SDL_free)> input(data,SDL_free);
        SDL_AudioSpec target{SDL_AUDIO_F32,2,48000};Uint8* converted=nullptr;int size=0;
        if(!SDL_ConvertAudioSamples(&spec,data,static_cast<int>(length),&target,&converted,&size))throw std::runtime_error(SDL_GetError());
        std::unique_ptr<Uint8,decltype(&SDL_free)> output(converted,SDL_free);
        if(size<=0 || size>128*1024*1024)throw std::runtime_error("Invalid or oversized audio data");
        if(cachedBytes+size>256*1024*1024)throw std::runtime_error("Audio cache exceeds 256 MiB budget");
        cachedBytes+=size;
        const float* samples=reinterpret_cast<float*>(converted);
        return cache.emplace(path,std::vector<float>(samples,samples+size/sizeof(float))).first->second;
    }
public:
    std::string error;
    ~AudioSystem(){if(stream)SDL_DestroyAudioStream(stream);}
    void clear(){voices.clear();cache.clear();cachedBytes=0;if(stream)SDL_ClearAudioStream(stream);}
    void start(World& world){clear();world.each<AudioSource>([](Entity,AudioSource& a){a.stop();if(a.playOnStart)a.play();});}
    void update(World& world,const AssetManager& assets,bool playing) {
        if(!playing){if(stream)SDL_ClearAudioStream(stream);return;}
        if(root!=assets.root){clear();root=assets.root;}
        if(!stream){
            if(!SDL_InitSubSystem(SDL_INIT_AUDIO)){error=SDL_GetError();return;}
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(!stream){error=SDL_GetError();return;}SDL_ResumeAudioStreamDevice(stream);
        }
        world.each<AudioSource>([&](Entity e,AudioSource& a){
            if(!std::isfinite(a.pitch))a.pitch=1;
            if(!std::isfinite(a.volume))a.volume=1;
            if(!std::isfinite(a.pan))a.pan=0;
            if(!std::isfinite(a.distance))a.distance=0;
            a.setPitch(a.pitch);a.setVolume(a.volume);a.setPan(a.pan);a.setDistance(a.distance);
            bool exists=false;for(auto& v:voices)if(v.entity==e && !v.shot){exists=true;if(v.restart!=a.restart || v.clip!=a.clip){v.cursor=0;v.restart=a.restart;v.clip=a.clip;}}
            if(!exists && voices.size()<128)voices.push_back({e,a.clip,0,a.restart,false});
            for(const auto& shot:a.shots)if(voices.size()<128)voices.push_back({e,shot,0,a.restart,true});
            a.shots.clear();
        });
        const int queued=SDL_GetAudioStreamQueued(stream);
        const int frames=std::clamp(2400-std::max(0,queued)/8,0,2400);if(!frames)return;
        std::vector<float> mix(frames*2,0);
        for(auto it=voices.begin();it!=voices.end();) {
            auto* a=world.tryGet<AudioSource>(it->entity);
            if(!a || (it->shot && it->restart!=a->restart) || (!it->shot && a->state==AudioSource::State::Stopped)){it=voices.erase(it);continue;}
            if(a->state==AudioSource::State::Paused){++it;continue;}
            bool ended=false;
            try{
                const auto& samples=load(assets,it->clip);const auto count=samples.size()/2;
                for(int frame=0;frame<frames;++frame){
                    if(it->cursor>=count){if(!it->shot && a->looping)it->cursor=std::fmod(it->cursor,double(count));else{ended=true;break;}}
                    if(!it->shot && a->fadeSeconds>0){float dt=1.0f/48000;float step=std::min(dt,a->fadeSeconds);a->fade+=(a->fadeTarget-a->fade)*step/a->fadeSeconds;a->fadeSeconds-=step;if(a->fadeSeconds<=0 && a->stopAfterFade){a->stop();ended=true;break;}}
                    auto index=static_cast<std::size_t>(it->cursor);auto next=std::min(index+1,count-1);float blend=it->cursor-index;
                    float gain=a->muted?0:a->volume*a->fade/(1+a->distance);
                    for(int channel=0;channel<2;++channel){float pan=channel==0?std::min(1.0f,1-a->pan):std::min(1.0f,1+a->pan);mix[frame*2+channel]+=(samples[index*2+channel]*(1-blend)+samples[next*2+channel]*blend)*gain*pan;}
                    it->cursor+=a->pitch;
                }
            }catch(const std::exception& e){error=e.what();ended=true;}
            if(ended){if(!it->shot)a->state=AudioSource::State::Stopped;it=voices.erase(it);}else ++it;
        }
        for(auto& value:mix)value=std::clamp(value,-1.0f,1.0f);
        if(!SDL_PutAudioStreamData(stream,mix.data(),static_cast<int>(mix.size()*sizeof(float))))error=SDL_GetError();
    }
};
