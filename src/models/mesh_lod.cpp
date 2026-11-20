#include "models/model_asset.hpp"
#include <meshoptimizer.h>
#include <numeric>

// Explicit asset-authoring operation; never run while importing or rendering.
// Original geometry, collision, skinning and lightmap topology remain untouched.
void generateMeshLODs(const AssetManager& assets, const std::string& path) {
    auto document = modelDocument(assets, path);
    if (document.at("version") != 1 ||
        (document.contains("bones") && !document.at("bones").empty())) {
        throw std::runtime_error(
            "Generate LODs on an unskinned, single-mesh asset (not a model container)");
    }
    const auto mesh = loadModelAsset(assets, path);
    if (mesh.vertices.size() < 24) {
        throw std::runtime_error("Mesh is already too small to simplify");
    }
    std::vector<unsigned> remap(mesh.vertices.size()), indices(mesh.vertices.size());
    const auto count = meshopt_generateVertexRemap(remap.data(),
                                                   nullptr,
                                                   mesh.vertices.size(),
                                                   mesh.vertices.data(),
                                                   mesh.vertices.size(),
                                                   sizeof(Vertex));
    std::vector<Vertex> vertices(count);
    meshopt_remapVertexBuffer(
        vertices.data(), mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(indices.data(), nullptr, mesh.vertices.size(), remap.data());
    Json levels = Json::array();
    for (int level = 1; level <= 2; ++level) {
        std::vector<unsigned> simplified(indices.size());
        const std::size_t target = std::max(
            std::size_t(3), (std::size_t(indices.size() * (level == 1 ? .5 : .2)) / 3) * 3);
        const auto size = meshopt_simplify(simplified.data(),
                                           indices.data(),
                                           indices.size(),
                                           &vertices[0].position.x,
                                           vertices.size(),
                                           sizeof(Vertex),
                                           target,
                                           .02f,
                                           meshopt_SimplifyLockBorder);
        if (!size || size >= indices.size()) {
            continue;
        }
        Json output = {{"format", "LegacyEngineMesh"},
                       {"version", 1},
                       {"importScale", 1},
                       {"vertices", Json::array()}};
        for (std::size_t i = 0; i < size; ++i) {
            const auto& v = vertices.at(simplified[i]);
            output["vertices"].push_back({v.position.x,
                                          v.position.y,
                                          v.position.z,
                                          v.normal.x,
                                          v.normal.y,
                                          v.normal.z,
                                          v.u,
                                          v.v});
        }
        // Reserve unused names, never overwrite user-authored meshes.
        const auto base =
            std::filesystem::path(path).parent_path() /
            (std::filesystem::path(path).stem().string() + "_LOD" + std::to_string(level));
        auto destination = base.generic_string() + ".mesh";
        for (unsigned n = 2; std::filesystem::exists(assets.resolve(destination)); ++n) {
            destination = base.generic_string() + "_" + std::to_string(n) + ".mesh";
        }
        writeJsonFile(assets.resolve(destination), output);
        levels.push_back(
            {{"mesh", destination}, {"distance", level == 1 ? 20 : 50}, {"vertices", size}});
    }
    if (levels.empty()) {
        throw std::runtime_error("No safe reduction found (borders/seams are preserved)");
    }
    document["lods"] = levels;
    float scale = modelImportScale(document);
    if (document.contains("settings")) {
        scale *=
            modelImportScale(modelDocument(assets, document.at("settings").get<std::string>()));
    }
    document["lodSourceScale"] = scale;
    writeJsonFile(assets.resolve(path), document); // publish only once both levels are complete
    ++modelAssetRevision;
}
