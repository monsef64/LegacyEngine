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
inline unsigned modelAssetRevision = 0;
inline float modelImportScale(const Json& json) {
    const float value = json.value("importScale", 1.f);
    if (!std::isfinite(value) || value < .000001f || value > 1000000.f) {
        throw std::runtime_error("Import scale must be between 0.000001 and 1000000");
    }
    return value;
}
inline Json modelDocument(const AssetManager& assets, const std::string& path) {
    if (!modelAsset(path)) {
        throw std::runtime_error("Expected a .lemesh asset");
    }
    auto json = readJsonFile(assets.resolve(path), 256 * 1024 * 1024);
    if (json.at("format") != "LegacyEngineMesh" ||
        (json.at("version") != 1 && json.at("version") != 2)) {
        throw std::runtime_error("Invalid model asset");
    }
    return json;
}
inline MeshData
loadModelAsset(const AssetManager& assets, const std::string& path, unsigned depth = 0) {
    if (depth > 2) {
        throw std::runtime_error("Cyclic model reference");
    }
    const auto json = modelDocument(assets, path);
    MeshData mesh;
    if (json.at("version") == 2) {
        const auto& parts = json.at("parts");
        if (!parts.is_array() || parts.empty() || parts.size() > 1024) {
            throw std::runtime_error("Invalid model parts");
        }
        for (const auto& part : parts) {
            auto data = loadModelAsset(assets, part.at("mesh").get<std::string>(), depth + 1);
            if (mesh.vertices.size() + data.vertices.size() > maxModelVertices) {
                throw std::runtime_error("Model vertex limit exceeded");
            }
            mesh.vertices.insert(mesh.vertices.end(), data.vertices.begin(), data.vertices.end());
        }
        return mesh;
    }
    float scale = modelImportScale(json);
    if (json.contains("settings")) {
        scale *= modelImportScale(modelDocument(assets, json.at("settings").get<std::string>()));
    }
    const auto& vertices = json.at("vertices");
    if (!vertices.is_array() || vertices.empty() || vertices.size() > maxModelVertices ||
        vertices.size() % 3) {
        throw std::runtime_error("Invalid model vertices");
    }
    mesh.vertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        if (!v.is_array() || v.size() != 8) {
            throw std::runtime_error("Invalid model vertex");
        }
        Vertex vertex{
            {v[0].get<float>() * scale, v[1].get<float>() * scale, v[2].get<float>() * scale},
            {v[3].get<float>(), v[4].get<float>(), v[5].get<float>()},
            v[6].get<float>(),
            v[7].get<float>()};
        if (!validModelVertex(vertex)) {
            throw std::runtime_error("Invalid scaled model coordinates");
        }
        mesh.vertices.push_back(vertex);
    }
    return mesh;
}
inline void saveModelImportScale(const AssetManager& assets, const std::string& path, float scale) {
    auto json = modelDocument(assets, path);
    const float oldScale = modelImportScale(json);
    json["importScale"] = scale;
    modelImportScale(json);
    const auto mesh = loadModelAsset(assets, path);
    for (auto vertex : mesh.vertices) {
        vertex.position = vertex.position * (scale / oldScale);
        if (!validModelVertex(vertex)) {
            throw std::runtime_error("Import scale would exceed valid geometry bounds");
        }
    }
    writeJsonFile(assets.resolve(path), json);
    ++modelAssetRevision;
}
