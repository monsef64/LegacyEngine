#include "models/model_import.hpp"
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

std::string modelImportExtensions() {
    static const auto result = [] {
        Assimp::Importer importer;
        aiString extensions;
        importer.GetExtensionList(extensions);
        std::string value = extensions.C_Str();
        for (std::size_t p = 0; (p = value.find("*.", p)) != std::string::npos;) {
            value.erase(p, 2);
        }
        return value;
    }();
    return result;
}
bool importableModel(const std::filesystem::path& path) {
    Assimp::Importer importer;
    return importer.IsExtensionSupported(AssetManager::extension(path));
}
std::string
importModel(AssetManager& assets, const std::filesystem::path& source, const std::string& folder) {
    if (!std::filesystem::is_regular_file(source) ||
        std::filesystem::file_size(source) > 256 * 1024 * 1024) {
        throw std::runtime_error("Model source must be a file smaller than 256 MiB");
    }
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(
        source.string(),
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices |
            aiProcess_ValidateDataStructure | aiProcess_SortByPType);
    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error(std::string("Assimp import failed: ") + importer.GetErrorString());
    }
    Json vertices = Json::array();
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const auto* mesh = scene->mMeshes[m];
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const auto& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue;
            }
            if (vertices.size() + 3 > maxModelVertices) {
                throw std::runtime_error("Model exceeds 500,000 triangles");
            }
            for (unsigned i = 0; i < 3; ++i) {
                const auto index = face.mIndices[i];
                const auto& p = mesh->mVertices[index];
                const auto n = mesh->HasNormals() ? mesh->mNormals[index] : aiVector3D(0, 1, 0);
                const auto uv =
                    mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][index] : aiVector3D();
                Vertex vertex{{p.x, p.y, p.z}, {n.x, n.y, n.z}, uv.x, uv.y};
                if (!validModelVertex(vertex)) {
                    throw std::runtime_error("Invalid model vertex values");
                }
                vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
            }
        }
    }
    if (vertices.empty()) {
        throw std::runtime_error(
            "Model contains no triangle geometry (point clouds and lines are not supported)");
    }
    const auto prefix = std::filesystem::path(folder);
    const auto stem = source.stem().string();
    auto path = (prefix / (stem + ".lemesh")).generic_string();
    for (unsigned i = 2; std::filesystem::exists(assets.resolve(path)); ++i) {
        path = (prefix / (stem + " (" + std::to_string(i) + ").lemesh")).generic_string();
    }
    std::filesystem::create_directories(assets.resolve(path).parent_path());
    writeJsonFile(
        assets.resolve(path),
        Json{{"format", "LegacyEngineMesh"}, {"version", 1}, {"vertices", std::move(vertices)}});
    // The baked representation must satisfy the same bound as the asset reader.
    if (std::filesystem::file_size(assets.resolve(path)) > 256 * 1024 * 1024) {
        std::filesystem::remove(assets.resolve(path));
        throw std::runtime_error("Baked model exceeds 256 MiB; simplify the source geometry");
    }
    return path;
}
