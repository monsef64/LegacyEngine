#pragma once
#include "audio/audio_clip.hpp"
#include "audio/audio_output.hpp"
class AudioPreview {
    std::unique_ptr<AudioOutput> output_;
    std::vector<float> samples_;
    std::size_t cursor_=0;
    bool nullDevice_=false;
    void pump(){
        if(!output_)return;
        const auto queued=output_->queuedFrames();
        const auto frames=queued<2400?std::min<std::size_t>(2400-queued,(samples_.size()-cursor_)/2):0;
        if(frames){
            output_->submit(std::vector<float>(samples_.begin()+cursor_,samples_.begin()+cursor_+frames*2));
            cursor_+=frames*2;
        }
        if(cursor_==samples_.size() && output_->queuedFrames()==0)stop();
    }
public:
    explicit AudioPreview(bool nullDevice=false):nullDevice_(nullDevice){}
    std::string path,error;
    ~AudioPreview(){stop();}
    void stop(){output_.reset();samples_.clear();cursor_=0;path.clear();}
    void update(){try{pump();}catch(const std::exception& e){stop();error=e.what();}}
    bool playing(const std::string& asset){update();return output_ && path==asset;}
    void toggle(const AssetManager& assets,const std::string& asset){
        if(playing(asset)){stop();return;}stop();
        try{
            samples_=decodeAudioClip(assets,asset);
            output_=std::make_unique<AudioOutput>(nullDevice_);
            path=asset;error.clear();pump();
            if(output_)output_->open();
        }catch(const std::exception& e){stop();error=e.what();}
    }
};
