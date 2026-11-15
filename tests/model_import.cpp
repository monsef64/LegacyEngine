#include "models/model_import.hpp"
#include <assimp/Importer.hpp>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

int main() {
    Assimp::Importer importer;
    assert(importer.IsExtensionSupported(".obj"));
    assert(importer.IsExtensionSupported(".gltf"));
    assert(importer.IsExtensionSupported(".glb"));
    assert(!importer.IsExtensionSupported(".fbx"));
    assert(!importer.IsExtensionSupported(".stl"));
    assert(modelImportExtensions() == "obj;gltf;glb");
    assert(importableModel("MODEL.OBJ") && importableModel("model.gltf") &&
           importableModel("model.GLB") && !importableModel("model.fbx"));
    const auto root = std::filesystem::temp_directory_path() /
                      ("legacy-model-import-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root / "assets");
    AssetManager assets;
    assets.root = root / "assets";
    const auto obj = root / "triangle.obj";
    {
        std::ofstream file(obj);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    const auto importedObj = importModel(assets, obj);
    assert(loadModelAsset(assets, importedObj).vertices.size() == 3);
    bool rejected = false;
    try {
        importModel(assets, root / "bad.fbx");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
    for (const char* name : {"animated_triangle.gltf", "animated_triangle.glb"}) {
        const auto gltf = std::filesystem::path(LEGACY_FIXTURES_DIR) / name;
        const auto importedGltf = importModel(assets, gltf);
        const auto document = modelDocument(assets, importedGltf);
        assert(document.at("animations").size() == 1);
        const auto bind = loadModelAsset(assets, importedGltf);
        const auto moved = loadModelAsset(assets, importedGltf, 0, 0, 1);
        assert(bind.vertices.size() == 3 && moved.vertices.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            assert(std::abs(moved.vertices[i].position.x - bind.vertices[i].position.x - 2) <
                   .001f);
        }
    }
}
