#pragma once
#include "hierarchy.hpp"
#include <algorithm>

inline Quat rotationFromEuler(Vec3 degrees) {
    return composeRotation(composeRotation(axisAngle({0,1,0},radians(degrees.y)),axisAngle({1,0,0},radians(degrees.x))),axisAngle({0,0,1},radians(degrees.z)));
}
inline Vec3 rotationToEuler(Quat q) {
    const auto r=rotationMatrix(normalize(q));
    const float pitch=std::asin(std::clamp(-r.m[7],-1.0f,1.0f));
    const bool pole=std::abs(std::cos(pitch))<.0001f;
    return {pitch*180/kPi,(pole?std::atan2(-r.m[2],r.m[0]):std::atan2(r.m[6],r.m[8]))*180/kPi,
        (pole?0:std::atan2(r.m[1],r.m[4]))*180/kPi};
}
struct RotationEditorState {
    Entity entity{};Quat last{};Vec3 angles{};
    void sync(Entity current,Quat rotation) {
        auto a=normalize(last),b=normalize(rotation);
        const float similarity=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
        if(entity!=current || similarity<.9999999f){entity=current;angles=rotationToEuler(rotation);last=rotation;}
    }
    Quat apply(Vec3 value) {value={std::clamp(value.x,-360.0f,360.0f),std::clamp(value.y,-360.0f,360.0f),std::clamp(value.z,-360.0f,360.0f)};angles=value;last=rotationFromEuler(value);return last;}
};
