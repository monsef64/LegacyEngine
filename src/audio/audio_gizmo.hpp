#pragma once
#include "rendering/mesh.hpp"
#include "audio/audio_source.hpp"
#include <cmath>

// Scene-only speaker glyph. No solid surfaces or gameplay geometry.
inline MeshData audioSourceGlyph(){
    MeshData mesh;mesh.topology=MeshData::Topology::Lines;
    auto line=[&](Vec3 a,Vec3 b){mesh.vertices.push_back({a,{}});mesh.vertices.push_back({b,{}});};
    const Vec3 points[]{{-.45f,-.2f,0},{-.15f,-.2f,0},{.2f,-.5f,0},{.2f,.5f,0},{-.15f,.2f,0},{-.45f,.2f,0}};
    for(int i=0;i<6;++i)line(points[i],points[(i+1)%6]);
    for(float radius:{.45f,.7f})for(int i=0;i<16;++i){
        const float a=-kPi/3+2*kPi/3*i/16,b=-kPi/3+2*kPi/3*(i+1)/16;
        line({.2f+radius*std::cos(a),radius*std::sin(a),0},{.2f+radius*std::cos(b),radius*std::sin(b),0});
    }
    return mesh;
}
// Distance is a world-space hearing range, not scaled by the owner's Transform.
inline MeshData audioSourceRange(const AudioSource& source){
    MeshData mesh;mesh.topology=MeshData::Topology::Lines;
    if(!std::isfinite(source.distance) || source.distance<=0)return mesh;
    const float radius=std::min(source.distance,10000.0f);
    constexpr int segments=64,meridians=12,latitudes=7;
    auto line=[&](Vec3 a,Vec3 b){mesh.vertices.push_back({a,{}});mesh.vertices.push_back({b,{}});};
    for(int meridian=0;meridian<meridians;++meridian){float longitude=2*kPi*meridian/meridians;
        auto point=[&](int i){float latitude=-kPi*.5f+kPi*i/segments;return Vec3{radius*std::cos(latitude)*std::cos(longitude),radius*std::sin(latitude),radius*std::cos(latitude)*std::sin(longitude)};};
        for(int i=0;i<segments;++i)line(point(i),point(i+1));
    }
    for(int ring=1;ring<=latitudes;++ring){float latitude=-kPi*.5f+kPi*ring/(latitudes+1);float height=radius*std::sin(latitude),r=radius*std::cos(latitude);
        auto point=[&](int i){float longitude=2*kPi*i/segments;return Vec3{r*std::cos(longitude),height,r*std::sin(longitude)};};
        for(int i=0;i<segments;++i)line(point(i),point(i+1));
    }
    return mesh;
}
