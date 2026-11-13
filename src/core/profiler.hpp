#pragma once
#include <array>
#include <chrono>
#include <algorithm>
#include <cstddef>

enum class ProfilePart:std::size_t {Events,Editor,Scripts,Hierarchy,Physics,Cameras,Audio,Scene,Game,UI,Present,Other,Lights,Skybox,Surfaces,Count};
inline constexpr const char* profileNames[]{"Events / input","Editor / panels / assets","Scripts (update)","Hierarchy","Physics + fixed scripts","Camera updates","Audio + pose sync","Scene render + gizmos","Game render + debug","ImGui GPU submission","Present / vsync wait","Other frame work","Render: light/shadow setup (nested)","Render: skybox (nested)","Render: surfaces / gizmos (nested)"};
class FrameProfiler {
public:
    static constexpr std::size_t parts=std::size_t(ProfilePart::Count),capacity=120;
    using Clock=std::chrono::steady_clock;
    struct Sample{std::array<double,parts> ms{};double total=0;};
    struct Stats{double last=0,average=0,peak=0;};
    bool paused=false;
    std::array<Sample,capacity> history{};
    std::size_t count=0,next=0;
    bool recording=false;
private:
    Sample current_{};Clock::time_point start_,checkpoint_;
public:
    void begin(bool enabled){recording=enabled && !paused;if(recording){current_={};start_=checkpoint_=Clock::now();}}
    void add(ProfilePart part,double milliseconds){if(recording)current_.ms[std::size_t(part)]+=std::max(0.0,milliseconds);}
    void checkpoint(ProfilePart part){if(!recording)return;auto now=Clock::now();add(part,std::chrono::duration<double,std::milli>(now-checkpoint_).count());checkpoint_=now;}
    void end(){if(!recording)return;checkpoint(ProfilePart::Other);current_.total=std::chrono::duration<double,std::milli>(Clock::now()-start_).count();history[next]=current_;next=(next+1)%capacity;count=std::min(count+1,capacity);recording=false;}
    Stats stats(int part=-1)const{Stats result;if(!count)return result;auto value=[&](const Sample& sample){return part<0?sample.total:sample.ms[std::size_t(part)];};result.last=value(history[(next+capacity-1)%capacity]);for(std::size_t i=0;i<count;++i){auto ms=value(history[i]);result.average+=ms;result.peak=std::max(result.peak,ms);}result.average/=count;return result;}
};
inline FrameProfiler* activeProfiler=nullptr;
class ProfileScope {
    FrameProfiler* profiler_;ProfilePart part_;FrameProfiler::Clock::time_point start_;
public:
    explicit ProfileScope(ProfilePart part):profiler_(activeProfiler && activeProfiler->recording?activeProfiler:nullptr),part_(part){if(profiler_)start_=FrameProfiler::Clock::now();}
    ~ProfileScope(){if(profiler_)profiler_->add(part_,std::chrono::duration<double,std::milli>(FrameProfiler::Clock::now()-start_).count());}
};
