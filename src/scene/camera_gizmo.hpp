#pragma once
#include "core/components.hpp"
#include "scene/scene_camera.hpp"
// Scene-only view volume: the camera's real FOV/aspect/clips, looking down -Z.
inline MeshData cameraFrustumWire(const Camera& camera) {
    MeshData mesh;
    mesh.topology = MeshData::Topology::Lines;
    if (!validCamera(camera) || !std::isfinite(camera.aspect) || camera.aspect <= 0) {
        return mesh;
    }
    auto line = [&](Vec3 a, Vec3 b) {
        mesh.vertices.push_back({a, {}});
        mesh.vertices.push_back({b, {}});
    };
    Vec3 corners[2][4];
    for (int plane = 0; plane < 2; ++plane) {
        float distance = plane ? camera.farClip : camera.nearClip;
        float height = camera.orthographic ? camera.orthographicSize * .5f
                                           : std::tan(radians(camera.fieldOfView) * .5f) * distance;
        float width = height * camera.aspect;
        corners[plane][0] = {-width, -height, -distance};
        corners[plane][1] = {width, -height, -distance};
        corners[plane][2] = {width, height, -distance};
        corners[plane][3] = {-width, height, -distance};
        for (int i = 0; i < 4; ++i) {
            line(corners[plane][i], corners[plane][(i + 1) % 4]);
        }
    }
    for (int i = 0; i < 4; ++i) {
        line(camera.orthographic ? corners[0][i] : Vec3{}, corners[1][i]);
    }
    return mesh;
}
