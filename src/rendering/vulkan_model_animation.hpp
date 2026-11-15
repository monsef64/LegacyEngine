#pragma once
#include "models/model_asset.hpp"
#include "models/skeleton_pose.hpp"

// Correctness-first CPU skinning for Vulkan scene meshes. Uses the same
// SkeletonPose evaluator (including crossfades and residual transforms) as the
// legacy GPU skinning path, then uploads the result to a Vulkan vertex buffer.
// Replace with the Vulkan compute shader once its runtime dispatch is wired.
inline MeshData loadVulkanAnimatedModel(const AssetManager& assets,
                                        const std::string& path,
                                        const Animation& animation,
                                        std::map<std::string, Json>& documents,
                                        unsigned depth = 0) {
    if (depth > 2) {
        throw std::runtime_error("Cyclic model reference");
    }
    const auto& part = cachedModelDocument(assets, path, &documents);
    if (part.at("version") == 2) {
        const auto& parts = part.at("parts");
        if (!parts.is_array() || parts.empty() || parts.size() > 1024) {
            throw std::runtime_error("Invalid model parts");
        }
        MeshData combined;
        for (const auto& child : parts) {
            auto mesh = loadVulkanAnimatedModel(
                assets, child.at("mesh").get<std::string>(), animation, documents, depth + 1);
            if (combined.vertices.size() + mesh.vertices.size() > maxModelVertices) {
                throw std::runtime_error("Model vertex limit exceeded");
            }
            combined.vertices.insert(
                combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        }
        return combined;
    }
    const auto& root =
        cachedModelDocument(assets, part.at("settings").get<std::string>(), &documents);
    const auto& vertices = part.at("vertices");
    if (!vertices.is_array() || vertices.empty() || vertices.size() > maxModelVertices ||
        vertices.size() % 3) {
        throw std::runtime_error("Invalid model vertices");
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
            throw std::runtime_error("Invalid model coordinates");
        }
        mesh.vertices.push_back(vertex);
    }
    const auto global = SkeletonPose(root).evaluate(animation);
    poseModelMeshGlobal(mesh, part, global, true);
    const float scale = modelImportScale(root) * modelImportScale(part);
    for (auto& vertex : mesh.vertices) {
        vertex.position = vertex.position * scale;
        if (!validModelVertex(vertex)) {
            throw std::runtime_error("Invalid posed geometry");
        }
    }
    return mesh;
}
