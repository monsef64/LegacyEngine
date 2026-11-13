#include "core/profiler.hpp"
#include <cassert>
#include <cmath>
int main(){
    FrameProfiler profiler;assert(profiler.count==0 && profiler.stats().average==0);
    profiler.begin(false);profiler.add(ProfilePart::Physics,4);profiler.end();assert(profiler.count==0);
    for(std::size_t i=0;i<FrameProfiler::capacity+5;++i){profiler.begin(true);profiler.add(ProfilePart::Physics,double(i));profiler.checkpoint(ProfilePart::Events);profiler.end();}
    assert(profiler.count==FrameProfiler::capacity && profiler.next==5);
    auto stats=profiler.stats(int(ProfilePart::Physics));assert(stats.last==124 && stats.peak==124 && stats.average==64.5);
    assert(std::isfinite(profiler.stats().average));
    profiler.paused=true;profiler.begin(true);profiler.add(ProfilePart::Physics,500);profiler.end();assert(profiler.stats(int(ProfilePart::Physics)).last==124);
    profiler.paused=false;activeProfiler=&profiler;profiler.begin(true);{ProfileScope scope(ProfilePart::Lights);}profiler.end();assert(profiler.stats(int(ProfilePart::Lights)).last>=0);activeProfiler=nullptr;
}
