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
    if(!std::isfinite(source.distance) || source.distance<=0 || !std::isfinite(source.blend) || source.blend<=0)return mesh;
    const float radius=std::min(source.distance,10000.0f);
    constexpr int segments=64;
    for(int axis=0;axis<3;++axis)for(int i=0;i<segments;++i){
        auto point=[&](int n){const float a=2*kPi*n/segments,x=radius*std::cos(a),y=radius*std::sin(a);return axis==0?Vec3{0,x,y}:axis==1?Vec3{x,0,y}:Vec3{x,y,0};};
        mesh.vertices.push_back({point(i),{}});mesh.vertices.push_back({point(i+1),{}});
    }
    return mesh;
}
