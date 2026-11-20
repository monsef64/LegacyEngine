#pragma once
#include "core/components.hpp"
#include <filesystem>
#include <memory>

namespace Navigation {
struct Settings {
    float cellSize = .3f, cellHeight = .2f, radius = .4f, height = 1.8f, climb = .4f, slope = 45;
};
struct Geometry {
    std::vector<float> vertices;
    std::vector<int> triangles;
    void append(const MeshData& mesh, const Transform& transform);
};
class System {
    struct Data;
    std::shared_ptr<Data> data_;
    unsigned revision_ = 1;
    struct Obstacle {
        Vec3 low, high;
    };
    std::vector<Obstacle> obstacles_;
    std::string obstacleState_;
    float obstacleClock_ = 0;
    bool blocked(Vec3 a, Vec3 b) const;
    bool basePath(Vec3 start, Vec3 destination, std::vector<Vec3>& corners) const;

  public:
    bool valid = false;
    std::string status;
    Settings settings;
    MeshData debugMesh; // line edges in world coordinates
    void clear();
    void bake(const Geometry& geometry, const Settings& options);
    void save(const std::filesystem::path& path, const std::string& signature) const;
    std::string load(const std::filesystem::path& path);
    void update(World& world, float seconds);
    bool path(Vec3 start, Vec3 destination, std::vector<Vec3>& corners) const;
    bool constrain(Vec3 start, Vec3 end, Vec3& result) const;
    void rebuildDebug();
};
} // namespace Navigation
