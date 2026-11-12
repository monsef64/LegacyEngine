#include "audio/audio_clip.hpp"
#include <SDL3/SDL.h>
#include <memory>
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
std::vector<float> decodeAudioClip(const AssetManager& assets,const std::string& path){
    const auto file=assets.resolve(path);
    if(!audioAsset(file) || std::filesystem::file_size(file)>32*1024*1024)throw std::runtime_error("Audio requires WAV or MP3 under 32 MiB");
    SDL_AudioSpec spec{};std::vector<float> mp3;Uint8* wav=nullptr;Uint32 wavSize=0;
    std::unique_ptr<Uint8,decltype(&SDL_free)> input(nullptr,SDL_free);
    const Uint8* data=nullptr;int size=0;
    if(AssetManager::extension(file)==".mp3"){
        drmp3 decoder;if(!drmp3_init_file(&decoder,file.string().c_str(),nullptr))throw std::runtime_error("Invalid MP3 clip");
        struct Close{drmp3* decoder;~Close(){drmp3_uninit(decoder);}} close{&decoder};
        if(decoder.channels<1 || decoder.channels>2)throw std::runtime_error("Unsupported MP3 channel count");
        spec={SDL_AUDIO_F32,static_cast<int>(decoder.channels),static_cast<int>(decoder.sampleRate)};
        std::vector<float> block(4096*decoder.channels);
        while(auto frames=drmp3_read_pcm_frames_f32(&decoder,4096,block.data())){
            if(mp3.size()+frames*decoder.channels>32*1024*1024)throw std::runtime_error("Decoded audio exceeds 128 MiB");
            mp3.insert(mp3.end(),block.begin(),block.begin()+frames*decoder.channels);
        }
        data=reinterpret_cast<const Uint8*>(mp3.data());size=static_cast<int>(mp3.size()*sizeof(float));
    }else{
        if(!SDL_LoadWAV(file.string().c_str(),&spec,&wav,&wavSize))throw std::runtime_error(SDL_GetError());
        input.reset(wav);data=wav;size=static_cast<int>(wavSize);
    }
    // Bound conversion before asking SDL to allocate the resampled buffer.
    const int bytes=SDL_AUDIO_BYTESIZE(spec.format)*spec.channels;
    if(bytes<=0 || spec.freq<=0 || size<=0 || double(size)/bytes/spec.freq*48000*8>128*1024*1024)throw std::runtime_error("Invalid or oversized decoded audio");
    SDL_AudioSpec target{SDL_AUDIO_F32,2,48000};Uint8* converted=nullptr;int outputSize=0;
    if(!SDL_ConvertAudioSamples(&spec,data,size,&target,&converted,&outputSize))throw std::runtime_error(SDL_GetError());
    std::unique_ptr<Uint8,decltype(&SDL_free)> output(converted,SDL_free);
    if(outputSize<=0 || outputSize>128*1024*1024)throw std::runtime_error("Invalid or oversized audio data");
    const auto* samples=reinterpret_cast<const float*>(converted);return {samples,samples+outputSize/sizeof(float)};
}
