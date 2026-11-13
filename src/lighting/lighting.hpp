#pragma once
#include "core/components.hpp"
#include <algorithm>
#include <vector>

namespace Lighting {
constexpr int maxLights=16,maxShadowLayers=12,shadowResolution=1024;
struct Light {
    Entity entity;
    LightSettings settings;
    Vec3 position,direction;
    int type=0,shadowLayer=-1; // directional=0, point=1, spot=2
    float priority=0;
};
// Conservative sphere/frustum test. The same test culls lights and shadow casters.
inline bool visibleSphere(const Mat4& vp,Vec3 center,float radius){
    for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
        const float a=vp.m[3]+sign*vp.m[axis],b=vp.m[7]+sign*vp.m[4+axis],c=vp.m[11]+sign*vp.m[8+axis],d=vp.m[15]+sign*vp.m[12+axis];
        if(a*center.x+b*center.y+c*center.z+d < -radius*std::sqrt(a*a+b*b+c*c))return false;
    }
    return true;
}
inline std::vector<Light> gather(World& world,const Mat4& vp,Vec3 eye){
    std::vector<Light> lights;
    auto collect=[&](int type,Entity e,const LightSettings& l,const Transform& t){
        if(!l.enabled || l.intensity<=0 || !validLight(l))return;
        if(type!=0 && !visibleSphere(vp,t.position,l.range))return;
        auto r=rotationMatrix(normalize(t.rotation));
        Light light{e,l,t.position,normalize(Vec3{-r.m[6],-r.m[7],-r.m[8]}),type};
        const auto delta=t.position-eye;
        light.priority=type==0?-1.0f:dot(delta,delta)/std::max(.01f,l.range*l.range);
        lights.push_back(light);
    };
    world.each<DirectionalLight,Transform>([&](Entity e,const DirectionalLight& l,const Transform& t){collect(0,e,l,t);});
    world.each<PointLight,Transform>([&](Entity e,const PointLight& l,const Transform& t){collect(1,e,l,t);});
    world.each<SpotLight,Transform>([&](Entity e,const SpotLight& l,const Transform& t){collect(2,e,l,t);});
    std::sort(lights.begin(),lights.end(),[](const Light& a,const Light& b){if(a.priority!=b.priority)return a.priority<b.priority;if(a.entity.index!=b.entity.index)return a.entity.index<b.entity.index;return a.type<b.type;});
    if(lights.size()>maxLights)lights.resize(maxLights);
    int layers=0;
    for(auto& l:lights){const int count=l.type==1?6:1;if(l.settings.shadows && layers+count<=maxShadowLayers){l.shadowLayer=layers;layers+=count;}}
    return lights;
}
inline Mat4 shadowMatrix(const Light& light,Vec3 eye,int face=0){
    const auto& l=light.settings;
    Vec3 direction=light.direction,up{0,1,0};
    if(light.type==1){
        const Vec3 directions[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        const Vec3 ups[]{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
        direction=directions[face];up=ups[face];
    }else if(std::abs(dot(direction,up))>.98f)up={0,0,1};
    if(light.type==0){
        const float extent=l.shadowDistance;
        // Snap the view anchor in light space to shadow texels to reduce shimmer.
        const Vec3 right=normalize(cross(direction,up)),vertical=cross(right,direction);
        const float texel=2*extent/shadowResolution;
        eye=eye+right*(std::floor(dot(eye,right)/texel)*texel-dot(eye,right))+
            vertical*(std::floor(dot(eye,vertical)/texel)*texel-dot(eye,vertical));
        Mat4 projection{};projection.m[0]=projection.m[5]=1/extent;projection.m[10]=-1/(2*extent);projection.m[14]=-1;projection.m[15]=1;
        return multiply(projection,lookAt(eye-direction*(2*extent),eye,up));
    }
    return multiply(perspective((light.type==1?90.0f:2*l.outerAngle)*kPi/180,1,.05f,l.range),lookAt(light.position,light.position+direction,up));
}
}
