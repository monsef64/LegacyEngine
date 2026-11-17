#pragma once
#include "editor/editor_geometry.hpp"
#include "editor/editor_history.hpp"
#include "scene/scene_camera.hpp"
#include "terrain/terrain_brushes.hpp"
#include "terrain/terrain_tools.hpp"
namespace Terrains {
class Editor {
    Entity selected_{}, stroke_{};
    bool visible_ = true, enabled_ = false;
    int tool_ = 0, layer_ = 0, resolution_ = 1, brushShape_ = 0;
    float radius_ = 5, strength_ = 5, falloff_ = .5f, flatten_ = 0, size_ = 128, importScale_ = 50,
          importOffset_ = 0;
    std::string source_, heightmap_, error_;

  public:
    bool active(World& world, Entity selected) const {
        return enabled_ && visible_ && world.has<Terrain>(selected);
    }
    void finish(World& world, Entity selected, EditorHistory& history) {
        if (world.alive(stroke_)) {
            if (auto* t = world.tryGet<Terrain>(stroke_)) {
                t->sculpting = false;
            }
            history.commit(world, selected);
        }
        stroke_ = {};
    }
    // Where the brush ring lands, in picture pixels. The Qt Scene view paints
    // it; the projection maths is unchanged from the immediate-mode overlay.
    struct BrushRing {
        bool visible = false;
        float centreX = 0, centreY = 0;
        std::vector<std::pair<float, float>> outer, falloff;
    };
    BrushRing brushRing(World& world,
                        Entity selected,
                        const Ray* ray,
                        const Mat4& vp,
                        const EditorRect& rect) {
        BrushRing out;
        if (!ray || !active(world, selected)) {
            return out;
        }
        const auto& terrain = world.get<Terrain>(selected);
        const auto* t = world.tryGet<Transform>(selected);
        if (!terrain.data || !t || std::abs(t->scale.x * t->scale.y * t->scale.z) < 1e-8f) {
            return out;
        }
        Quat inverse{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
        auto origin = rotateVector(inverse, ray->origin - t->position),
             direction = rotateVector(inverse, ray->direction);
        origin = {origin.x / t->scale.x, origin.y / t->scale.y, origin.z / t->scale.z};
        direction = {direction.x / t->scale.x, direction.y / t->scale.y, direction.z / t->scale.z};
        Vec3 hit;
        const auto& d = *terrain.data;
        if (!intersect(d, origin, direction, hit)) {
            return out;
        }
        auto project = [&](float x, float z, std::pair<float, float>& p) {
            if (x < 0 || z < 0 || x > d.size || z > d.size) {
                return false;
            }
            float fx = x / d.size * (d.resolution - 1), fz = z / d.size * (d.resolution - 1);
            int ix = std::min(int(fx), d.resolution - 2), iz = std::min(int(fz), d.resolution - 2);
            float u = fx - ix, v = fz - iz;
            auto a = point(d, ix, iz), b = point(d, ix + 1, iz), c = point(d, ix, iz + 1),
                 e = point(d, ix + 1, iz + 1);
            float y = u + v <= 1 ? a.y * (1 - u - v) + b.y * u + c.y * v
                                 : b.y * (1 - v) + c.y * (1 - u) + e.y * (u + v - 1);
            auto worldPoint = t->position + rotateVector(t->rotation, Vec3{x, y, z} * t->scale);
            float w = vp.m[3] * worldPoint.x + vp.m[7] * worldPoint.y + vp.m[11] * worldPoint.z +
                      vp.m[15];
            if (w <= 1e-5f) {
                return false;
            }
            auto p3 = projectCameraPoint(vp, worldPoint);
            if (p3.z < -1 || p3.z > 1) {
                return false;
            }
            p = {rect.x + (p3.x + 1) * .5f * rect.width,
                 rect.y + (1 - p3.y) * .5f * rect.height};
            return true;
        };
        auto ring = [&](float radius, std::vector<std::pair<float, float>>& points) {
            std::pair<float, float> previous{};
            bool valid = false;
            for (int i = 0; i <= 128; ++i) {
                const float angle = i * (2 * kPi / 128);
                std::pair<float, float> p{};
                const bool next =
                    project(hit.x + std::cos(angle) * radius, hit.z + std::sin(angle) * radius, p);
                if (valid && next) {
                    points.push_back(p);
                }
                previous = p;
                valid = next;
            }
            (void)previous;
        };
        ring(radius_, out.outer);
        if (brushShape_ == 0 && falloff_ > 0) {
            ring(radius_ * std::sqrt(1 - std::pow(.5f, 1 / (falloff_ * 4))), out.falloff);
        }
        std::pair<float, float> centre{};
        out.visible = project(hit.x, hit.z, centre);
        out.centreX = centre.first;
        out.centreY = centre.second;
        return out;
    }
    // State the Qt Terrain panel reads and writes.
    bool& enabled() {
        return enabled_;
    }
    bool& visible() {
        return visible_;
    }
    int& tool() {
        return tool_;
    }
    int& layer() {
        return layer_;
    }
    int& brushShape() {
        return brushShape_;
    }
    int& resolution() {
        return resolution_;
    }
    float& radius() {
        return radius_;
    }
    float& strength() {
        return strength_;
    }
    float& falloff() {
        return falloff_;
    }
    float& flatten() {
        return flatten_;
    }
    float& size() {
        return size_;
    }
    float& importScale() {
        return importScale_;
    }
    float& importOffset() {
        return importOffset_;
    }
    std::string& source() {
        return source_;
    }
    std::string& heightmap() {
        return heightmap_;
    }
    std::string& error() {
        return error_;
    }
    static const std::array<const char*, 15>& brushLabels() {
        return Terrains::brushNames;
    }
    void stroke(World& world,
                Entity selected,
                EditorHistory& history,
                const Ray* ray,
                bool down,
                float dt) {
        if (!down) {
            if (stroke_ != Entity{}) {
                finish(world, selected, history);
            }
            return;
        }
        if (!ray || !active(world, selected)) {
            return;
        }
        auto& terrain = world.get<Terrain>(selected);
        const auto* t = world.tryGet<Transform>(selected);
        if (!terrain.data || !t || std::abs(t->scale.x * t->scale.y * t->scale.z) < 1e-8f) {
            return;
        }
        Quat inverse{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
        Vec3 origin = rotateVector(inverse, ray->origin - t->position),
             direction = rotateVector(inverse, ray->direction);
        origin = {origin.x / t->scale.x, origin.y / t->scale.y, origin.z / t->scale.z};
        direction = {direction.x / t->scale.x, direction.y / t->scale.y, direction.z / t->scale.z};
        Vec3 hit;
        if (!intersect(*terrain.data, origin, direction, hit)) {
            return;
        }
        if (stroke_ == Entity{}) {
            history.commit(world, selected);
            stroke_ = selected;
            terrain.sculpting = true;
        }
        brush(terrain,
              hit,
              tool_,
              radius_,
              strength_,
              falloff_,
              flatten_,
              layer_,
              std::clamp(dt, 0.f, .05f),
              brushShape_);
    }
};
} // namespace Terrains
