#pragma once
#include "editor/editor_history.hpp"
#include "scene/camera.hpp"
#include <filesystem>
#include <string_view>

// Compare saved editor content in linear time; no serialization/GPU handles are
// written to disk merely to determine whether the title needs a dirty marker.
inline std::string projectContentState(World& world,std::string_view source,const OrbitCamera& camera) {
    std::string state=sceneSignature(world);
    auto scalar=[&](auto v){state.append(reinterpret_cast<const char*>(&v),sizeof(v));};
    scalar(source.size());state.append(source.data(),source.size());
    scalar(camera.target.x);scalar(camera.target.y);scalar(camera.target.z);
    scalar(camera.yaw);scalar(camera.pitch);scalar(camera.distance);scalar(camera.fieldOfView);
    return state;
}
inline std::string projectWindowTitle(const std::string& path,bool dirty) {
    const auto name=path.empty()?"Untitled":std::filesystem::path(path).filename().string();
    return name+(dirty?"*":"")+" - LegacyEngine";
}
