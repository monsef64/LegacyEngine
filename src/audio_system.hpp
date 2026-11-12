#pragma once
#include "assets.hpp"
#include <SDL3/SDL.h>
#include <map>
#include <memory>

// Deterministic listener selection; disabled/missing-transform listeners cannot hear.
inline Entity activeAudioListener(World& world){
    Entity selected{};world.each<AudioListener,Transform>([&](Entity e,AudioListener& listener,Transform&){
        if(listener.enabled && (selected==Entity{} || e.index<selected.index))selected=e;
    });return selected;
}
struct AudioGains {float left=0,right=0;};
inline AudioGains spatialAudioGains(const AudioSource& source,const Transform* emitter,const Transform* listener){
    if(!listener || source.muted)return {};
    const float blend=std::clamp(source.blend,0.0f,1.0f);
    const float pan=std::clamp(source.pan,-1.0f,1.0f);
    const float left2=std::min(1.0f,1-pan),right2=std::min(1.0f,1+pan);
    float attenuation=0,spatialPan=0;
    if(emitter){
        const Vec3 delta=emitter->position-listener->position;
        const float distance=std::sqrt(dot(delta,delta));
        attenuation=source.distance>0?std::clamp(1-distance/source.distance,0.0f,1.0f):(distance==0?1.0f:0.0f);
        const auto rotation=rotationMatrix(normalize(listener->rotation));
        spatialPan=std::clamp(dot(normalize(delta),Vec3{rotation.m[0],rotation.m[1],rotation.m[2]})+pan,-1.0f,1.0f);
    }
    return {(1-blend)*left2+blend*attenuation*std::min(1.0f,1-spatialPan),
            (1-blend)*right2+blend*attenuation*std::min(1.0f,1+spatialPan)};
}

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
    // Deterministic rendering entry point, also used by dummy-device regression tests.
    std::vector<float> render(World& world,const AssetManager& assets,int frames){
        frames=std::clamp(frames,0,48000);
        voices.erase(std::remove_if(voices.begin(),voices.end(),[&](const Voice& voice){
            const auto* source=world.tryGet<AudioSource>(voice.entity);
            return !source || (voice.shot?voice.restart!=source->restart:source->state==AudioSource::State::Stopped);
        }),voices.end());
        world.each<AudioSource>([&](Entity e,AudioSource& a){
            if(!std::isfinite(a.pitch))a.pitch=1;
            if(!std::isfinite(a.volume))a.volume=1;
            if(!std::isfinite(a.pan))a.pan=0;
            if(!std::isfinite(a.distance))a.distance=0;
            if(!std::isfinite(a.blend))a.blend=0;
            a.spatialBlend(a.blend);a.setPitch(a.pitch);a.setVolume(a.volume);a.setPan(a.pan);a.setDistance(a.distance);
            bool exists=false;for(auto& v:voices)if(v.entity==e && !v.shot){exists=true;if(v.restart!=a.restart || v.clip!=a.clip){v.cursor=0;v.restart=a.restart;v.clip=a.clip;}}
            if(!exists && a.state!=AudioSource::State::Stopped && voices.size()<128)voices.push_back({e,a.clip,0,a.restart,false});
            for(const auto& shot:a.shots)if(voices.size()<128)voices.push_back({e,shot,0,a.restart,true});
            a.shots.clear();
        });
        struct Envelope {std::vector<float> samples;AudioSource result;bool finishedFade=false;};
        std::map<std::uint64_t,Envelope> envelopes;
        for(const auto& voice:voices){
            const auto key=(std::uint64_t(voice.entity.generation)<<32)|voice.entity.index;
            if(envelopes.count(key))continue;
            const auto& source=world.get<AudioSource>(voice.entity);
            Envelope envelope;envelope.result=source;envelope.samples.reserve(frames);
            // A source can have only one-shots remaining after its primary clip ends.
            if(envelope.result.state==AudioSource::State::Stopped)envelope.result.state=AudioSource::State::Playing;
            for(int i=0;i<frames;++i){
                envelope.samples.push_back(envelope.result.fade);
                envelope.result.advanceFade(1.0f/48000);
                if(envelope.result.state==AudioSource::State::Stopped){envelope.finishedFade=true;envelope.samples.resize(frames,0);break;}
            }
            envelopes.emplace(key,std::move(envelope));
        }
        const auto listenerEntity=activeAudioListener(world);
        const auto* listener=world.tryGet<Transform>(listenerEntity);
        std::vector<float> mix(frames*2,0);
        for(auto it=voices.begin();it!=voices.end();) {
            auto* a=world.tryGet<AudioSource>(it->entity);
            if(!a || (it->shot && it->restart!=a->restart) || (!it->shot && a->state==AudioSource::State::Stopped)){it=voices.erase(it);continue;}
            if(a->state==AudioSource::State::Paused){++it;continue;}
            const auto gains=spatialAudioGains(*a,world.tryGet<Transform>(it->entity),listener);
            bool ended=false;
            try{
                const auto& samples=load(assets,it->clip);const auto count=samples.size()/2;
                for(int frame=0;frame<frames;++frame){
                    if(it->cursor>=count){if(!it->shot && a->looping)it->cursor=std::fmod(it->cursor,double(count));else{ended=true;break;}}

                    auto index=static_cast<std::size_t>(it->cursor);auto next=std::min(index+1,count-1);float blend=it->cursor-index;
                    const auto& envelope=envelopes.at((std::uint64_t(it->entity.generation)<<32)|it->entity.index);
                    float gain=a->volume*envelope.samples[frame];
                    for(int channel=0;channel<2;++channel){float pan=channel==0?gains.left:gains.right;mix[frame*2+channel]+=(samples[index*2+channel]*(1-blend)+samples[next*2+channel]*blend)*gain*pan;}
                    it->cursor+=a->pitch;
                }
            }catch(const std::exception& e){error=e.what();ended=true;}
            if(ended){if(!it->shot)a->state=AudioSource::State::Stopped;it=voices.erase(it);}else ++it;
        }
        for(auto& value:mix)value=std::clamp(value,-1.0f,1.0f);
        world.each<AudioSource>([&](Entity e,AudioSource& source){
            const auto found=envelopes.find((std::uint64_t(e.generation)<<32)|e.index);
            if(found==envelopes.end())return;
            const auto& result=found->second.result;
            source.fade=result.fade;source.fadeSeconds=result.fadeSeconds;
            if(found->second.finishedFade)source.stop();
        });
        return mix;
    }
    void update(World& world,const AssetManager& assets,bool playing) {
        if(!playing){if(stream)SDL_ClearAudioStream(stream);return;}
        if(root!=assets.root){clear();root=assets.root;}
        if(!stream){
            if(!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)){error=SDL_GetError();return;}
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(!stream){error=SDL_GetError();return;}SDL_ResumeAudioStreamDevice(stream);
        }
        const int queued=SDL_GetAudioStreamQueued(stream);
        const int frames=std::clamp(2400-std::max(0,queued)/8,0,2400);if(!frames)return;
        auto mix=render(world,assets,frames);
        if(!SDL_PutAudioStreamData(stream,mix.data(),static_cast<int>(mix.size()*sizeof(float))))error=SDL_GetError();
    }
};
