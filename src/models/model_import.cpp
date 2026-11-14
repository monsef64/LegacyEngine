#include "models/model_import.hpp"
#include "materials/material_io.hpp"
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <map>

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
    importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
    const auto* scene = importer.ReadFile(
        source.string(),
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices |
            aiProcess_ValidateDataStructure | aiProcess_SortByPType);
    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error(std::string("Assimp import failed: ") + importer.GetErrorString());
    }
    const auto prefix = std::filesystem::path(folder);
    const auto stem = source.stem().string();
    auto path = (prefix / (stem + ".lemesh")).generic_string();
    for (unsigned i = 2; std::filesystem::exists(assets.resolve(path)); ++i) {
        path = (prefix / (stem + " (" + std::to_string(i) + ").lemesh")).generic_string();
    }
    std::string dataFolder = (std::filesystem::path(path).parent_path() /
                              (std::filesystem::path(path).stem().string() + "_data"))
                                 .generic_string();
    for (unsigned i = 2; std::filesystem::exists(assets.resolve(dataFolder)); ++i) {
        dataFolder = (prefix / (stem + "_data_" + std::to_string(i))).generic_string();
    }
    std::filesystem::create_directories(assets.resolve(dataFolder));
    Json parts = Json::array();
    std::map<unsigned, std::string> materials;
    std::size_t count = 0;
    std::string warnings;
    try {
        auto texture =
            [&](const aiMaterial* material, aiTextureType type, const std::string& label) {
                aiString name;
                if (material->GetTexture(type, 0, &name) != AI_SUCCESS) {
                    return std::string{};
                }
                try {
                    if (const auto* embedded = scene->GetEmbeddedTexture(name.C_Str())) {
                        const std::string ext =
                            embedded->mHeight == 0 ? std::string(embedded->achFormatHint) : "tga";
                        const auto file =
                            dataFolder + "/" + label + "." + (ext == "jpeg" ? "jpg" : ext);
                        if (!materialTexturePath(file)) {
                            throw std::runtime_error("Unsupported embedded texture format");
                        }
                        std::ofstream out(assets.resolve(file), std::ios::binary);
                        if (!embedded->mHeight) {
                            if (embedded->mWidth > 256 * 1024 * 1024) {
                                throw std::runtime_error("Embedded texture too large");
                            }
                            out.write(reinterpret_cast<const char*>(embedded->pcData),
                                      embedded->mWidth);
                        } else {
                            if (embedded->mWidth > 16384 || embedded->mHeight > 16384) {
                                throw std::runtime_error("Embedded texture too large");
                            }
                            unsigned char header[18]{};
                            header[2] = 2;
                            header[12] = embedded->mWidth & 255;
                            header[13] = embedded->mWidth >> 8;
                            header[14] = embedded->mHeight & 255;
                            header[15] = embedded->mHeight >> 8;
                            header[16] = 32;
                            header[17] = 0x28;
                            out.write(reinterpret_cast<const char*>(header), 18);
                            for (unsigned i = 0; i < embedded->mWidth * embedded->mHeight; ++i) {
                                const auto& p = embedded->pcData[i];
                                const unsigned char bgra[]{p.b, p.g, p.r, p.a};
                                out.write(reinterpret_cast<const char*>(bgra), 4);
                            }
                        }
                        out.close();
                        if (!out) {
                            throw std::runtime_error("Cannot copy embedded texture");
                        }
                        return file;
                    }
                    std::string textureName = name.C_Str();
                    std::replace(textureName.begin(), textureName.end(), '\\', '/');
                    const auto file = std::filesystem::path(textureName).is_absolute()
                                          ? std::filesystem::path(textureName)
                                          : source.parent_path() / textureName;
                    if (!materialTexturePath(file.filename().string())) {
                        throw std::runtime_error("Unsupported texture format");
                    }
                    return assets.import(file, dataFolder);
                } catch (const std::exception& error) {
                    warnings += std::string(name.C_Str()) + ": " + error.what() + "\n";
                    return std::string{};
                }
            };
        for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
            const auto* mesh = scene->mMeshes[m];
            Json vertices = Json::array();
            for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
                const auto& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) {
                    continue;
                }
                if ((count += 3) > maxModelVertices) {
                    throw std::runtime_error("Model exceeds 500,000 triangles");
                }
                for (unsigned i = 0; i < 3; ++i) {
                    const auto index = face.mIndices[i];
                    const auto& p = mesh->mVertices[index];
                    const auto n = mesh->HasNormals() ? mesh->mNormals[index] : aiVector3D(0, 1, 0);
                    const auto uv =
                        mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][index] : aiVector3D();
                    if (!validModelVertex({{p.x, p.y, p.z}, {n.x, n.y, n.z}, uv.x, uv.y})) {
                        throw std::runtime_error("Invalid model vertex");
                    }
                    vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
                }
            }
            if (vertices.empty()) {
                continue;
            }
            if (parts.size() >= 1024) {
                throw std::runtime_error("Model exceeds 1024 mesh parts");
            }
            const auto meshPath = dataFolder + "/Mesh_" + std::to_string(m) + ".lemesh";
            writeJsonFile(assets.resolve(meshPath),
                          {{"format", "LegacyEngineMesh"},
                           {"version", 1},
                           {"settings", path},
                           {"vertices", std::move(vertices)}});
            if (std::filesystem::file_size(assets.resolve(meshPath)) > 256 * 1024 * 1024) {
                throw std::runtime_error("Baked mesh too large");
            }
            auto found = materials.find(mesh->mMaterialIndex);
            if (found == materials.end()) {
                MaterialProperties material;
                const auto* sourceMaterial = scene->mMaterials[mesh->mMaterialIndex];
                aiColor3D color(1, 1, 1);
                sourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
                material.color = {std::clamp(color.r, 0.f, 100.f),
                                  std::clamp(color.g, 0.f, 100.f),
                                  std::clamp(color.b, 0.f, 100.f)};
                const auto label = "Material_" + std::to_string(mesh->mMaterialIndex);
                material.texture =
                    texture(sourceMaterial, aiTextureType_DIFFUSE, label + "_diffuse");
                material.normalMap =
                    texture(sourceMaterial, aiTextureType_NORMALS, label + "_normal");
                if (!material.normalMap.empty()) {
                    material.shader = 1;
                }
                const auto materialPath = dataFolder + "/" + label + ".mat";
                saveMaterial(assets, materialPath, material);
                found = materials.emplace(mesh->mMaterialIndex, materialPath).first;
            }
            parts.push_back(
                {{"name",
                  mesh->mName.length ? mesh->mName.C_Str() : ("Mesh " + std::to_string(m)).c_str()},
                 {"mesh", meshPath},
                 {"material", found->second}});
        }
        if (parts.empty()) {
            throw std::runtime_error("Model contains no triangle geometry");
        }
        writeJsonFile(assets.resolve(path),
                      {{"format", "LegacyEngineMesh"},
                       {"version", 2},
                       {"importScale", 1},
                       {"parts", std::move(parts)},
                       {"warnings", warnings}});
        ++modelAssetRevision;
        return path;
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(assets.resolve(dataFolder), error);
        throw;
    }
}
