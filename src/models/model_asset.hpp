#pragma once
#include "assets/assets.hpp"
#include "materials/material.hpp"
#include "project/json_io.hpp"
#include "rendering/mesh.hpp"

inline bool modelAsset(const std::string& path) {
    return materialPathValid(path) && AssetManager::extension(path) == ".lemesh";
}
constexpr std::size_t maxModelVertices = 1500000;
inline bool validModelVertex(const Vertex& v) {
    const float values[]{
        v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.u, v.v};
    for (float value : values) {
        if (!std::isfinite(value) || std::abs(value) > 1e6f) {
            return false;
        }
    }
    return true;
}
inline MeshData loadModelAsset(const AssetManager& assets, const std::string& path) {
    if (!modelAsset(path)) {
        throw std::runtime_error("Expected a .lemesh model asset");
    }
    const auto json = readJsonFile(assets.resolve(path), 256 * 1024 * 1024);
    const auto& vertices = json.at("vertices");
    if (json.at("format") != "LegacyEngineMesh" || json.at("version") != 1 ||
        !vertices.is_array() || vertices.empty() || vertices.size() > maxModelVertices ||
        vertices.size() % 3) {
        throw std::runtime_error("Invalid model asset");
    }
    MeshData mesh;
    mesh.vertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        if (!v.is_array() || v.size() != 8) {
            throw std::runtime_error("Invalid model vertex");
        }
        Vertex vertex{{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()},
                      {v[3].get<float>(), v[4].get<float>(), v[5].get<float>()},
                      v[6].get<float>(),
                      v[7].get<float>()};
        if (!validModelVertex(vertex)) {
            throw std::runtime_error("Nonfinite or oversized model coordinates");
        }
        mesh.vertices.push_back(vertex);
    }
    return mesh;
}
