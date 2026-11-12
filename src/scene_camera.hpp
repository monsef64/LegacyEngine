#pragma once
#include "hierarchy.hpp"
#include <algorithm>
#include <stdexcept>

inline Entity activeCamera(World& world) {
    Entity found;world.each<Camera,Transform>([&](Entity e,Camera& c,Transform&){if(c.active && found==Entity{})found=e;});return found;
}
inline void activateCamera(World& world,Entity entity) {
    if(!world.has<Camera>(entity) || !world.has<Transform>(entity))throw std::runtime_error("Expected a Camera GameObject");
    world.each<Camera>([&](Entity e,Camera& c){c.active=e==entity;});
}
inline void aimCamera(Transform& transform,Vec3 target) {
    auto d=target-transform.position;const float length=dot(d,d);
    if(length<1e-10f)return;
    d=d*(1/std::sqrt(length));
    transform.rotation=composeRotation(axisAngle({0,1,0},std::atan2(-d.x,-d.z)),axisAngle({1,0,0},std::asin(std::clamp(d.y,-1.0f,1.0f))));
}
inline bool validCamera(const Camera& c) {
    auto finite=[](float n){return std::isfinite(n) && std::abs(n)<=1e6;};
    return finite(c.fieldOfView) && c.fieldOfView>0 && c.fieldOfView<179 &&
        finite(c.nearClip) && finite(c.farClip) && c.nearClip>0 && c.farClip>c.nearClip &&
        finite(c.orthographicSize) && c.orthographicSize>0 &&
        finite(c.viewportX) && finite(c.viewportY) && finite(c.viewportWidth) && finite(c.viewportHeight) &&
        c.viewportX>=0 && c.viewportY>=0 && c.viewportWidth>0 && c.viewportHeight>0 &&
        c.viewportX+c.viewportWidth<=1.00001f && c.viewportY+c.viewportHeight<=1.00001f;
}
inline Vec3 cameraEye(const Transform& t,const Camera& c) {
    const float amount=c.shakeRemaining>0?c.shakeIntensity:0;
    return t.position+Vec3{std::sin(c.shakePhase*37)*amount,std::sin(c.shakePhase*53)*amount,std::sin(c.shakePhase*29)*amount*.5f};
}
inline Mat4 cameraMatrix(const Transform& t,const Camera& c) {
    if(!validCamera(c) || !std::isfinite(c.aspect) || c.aspect<=0)throw std::runtime_error("Invalid Camera projection");
    const float aspect=c.aspect*c.viewportWidth/c.viewportHeight;
    Mat4 projection{};
    if(c.orthographic) {
        const float h=c.orthographicSize,w=h*aspect;
        projection.m[0]=2/w;projection.m[5]=2/h;projection.m[10]=-2/(c.farClip-c.nearClip);
        projection.m[14]=-(c.farClip+c.nearClip)/(c.farClip-c.nearClip);projection.m[15]=1;
    } else projection=perspective(radians(c.fieldOfView),aspect,c.nearClip,c.farClip);
    const auto rotation=normalize(t.rotation);const Vec3 eye=cameraEye(t,c);
    return multiply(projection,lookAt(eye,eye+rotateVector(rotation,{0,0,-1}),rotateVector(rotation,{0,1,0})));
}
inline Mat4 inverseCameraMatrix(Mat4 matrix) {
    double rows[4][8]{};
    for(int r=0;r<4;++r){for(int c=0;c<4;++c)rows[r][c]=matrix.m[c*4+r];rows[r][4+r]=1;}
    for(int c=0;c<4;++c) {
        int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(rows[r][c])>std::abs(rows[pivot][c]))pivot=r;
        if(std::abs(rows[pivot][c])<1e-12)throw std::runtime_error("Camera matrix is singular");
        for(int k=0;k<8;++k)std::swap(rows[c][k],rows[pivot][k]);
        const double divisor=rows[c][c];for(double& value:rows[c])value/=divisor;
        for(int r=0;r<4;++r)if(r!=c){const double factor=rows[r][c];for(int k=0;k<8;++k)rows[r][k]-=factor*rows[c][k];}
    }
    Mat4 result{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)result.m[c*4+r]=static_cast<float>(rows[r][c+4]);return result;
}
inline Vec3 projectCameraPoint(const Mat4& matrix,Vec3 point) {
    const float w=matrix.m[3]*point.x+matrix.m[7]*point.y+matrix.m[11]*point.z+matrix.m[15];
    if(std::abs(w)<1e-8f)throw std::runtime_error("Point is on the camera projection plane");
    return {(matrix.m[0]*point.x+matrix.m[4]*point.y+matrix.m[8]*point.z+matrix.m[12])/w,
            (matrix.m[1]*point.x+matrix.m[5]*point.y+matrix.m[9]*point.z+matrix.m[13])/w,
            (matrix.m[2]*point.x+matrix.m[6]*point.y+matrix.m[10]*point.z+matrix.m[14])/w};
}
inline Vec3 cameraWorldToScreen(const Transform& t,const Camera& c,Vec3 world) {
    const auto p=projectCameraPoint(cameraMatrix(t,c),world);
    return {c.viewportX+(p.x+1)*.5f*c.viewportWidth,c.viewportY+(1-p.y)*.5f*c.viewportHeight,(p.z+1)*.5f};
}
inline Vec3 cameraScreenToWorld(const Transform& t,const Camera& c,Vec3 screen) {
    const Vec3 ndc{2*(screen.x-c.viewportX)/c.viewportWidth-1,1-2*(screen.y-c.viewportY)/c.viewportHeight,2*screen.z-1};
    return projectCameraPoint(inverseCameraMatrix(cameraMatrix(t,c)),ndc);
}
inline void updateCameras(World& world,float dt) {
    world.each<Camera,Transform>([&](Entity,Camera& c,Transform& t){
        if(c.tracking) {
            Vec3 target=c.target;
            if(c.targetEntity!=Entity{}) {
                auto* targetTransform=world.tryGet<Transform>(c.targetEntity);
                if(!targetTransform){c.tracking=false;c.following=false;c.targetEntity={};return;}
                target=targetTransform->position;
            }
            if(c.following)t.position=target+c.followOffset;
            aimCamera(t,target);
        }
        c.shakePhase+=std::max(0.0f,dt);c.shakeRemaining=std::max(0.0f,c.shakeRemaining-dt);
    });
}
