#include "models/model_import.hpp"
#include "materials/material_io.hpp"
#include <algorithm>
#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <functional>
#include <map>
#include <set>

namespace {
// GoldSrc models were authored against case-insensitive filesystems. Assimp
// synthesizes T.mdl / 01.mdl filenames; their case often differs in real packs.
class GoldSrcIOSystem final : public Assimp::DefaultIOSystem {
  public:
    static std::string existingPath(const char* filename) {
        auto path = std::filesystem::u8path(filename);
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
            return path.u8string();
        }
        auto parent = path.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        auto lower = [](std::string value) {
            for (auto& c : value) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            return value;
        };
        const auto wanted = lower(path.filename().u8string());
        std::string found;
        for (std::filesystem::directory_iterator it(parent, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && lower(it->path().filename().u8string()) == wanted) {
                if (!found.empty()) {
                    return filename; // ambiguous: never pick an arbitrary companion
                }
                found = it->path().u8string();
            }
        }
        return found.empty() ? filename : found;
    }

  public:
    bool Exists(const char* path) const override {
        return Assimp::DefaultIOSystem::Exists(existingPath(path).c_str());
    }
    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
        const auto resolved = existingPath(path);
        return Assimp::DefaultIOSystem::Open(resolved.c_str(), mode);
    }
};
} // namespace

std::string modelImportExtensions() {
    // Do not advertise internal Assimp dependencies (MD2 supplies MDL normals).
    return "obj;gltf;glb;mdl";
}

bool importableModel(const std::filesystem::path& path) {
    const auto extension = AssetManager::extension(path);
    return extension == ".obj" || extension == ".gltf" || extension == ".glb" ||
           extension == ".mdl";
}
bool modelCompanionFile(const std::filesystem::path& path) {
    if (AssetManager::extension(path) != ".mdl") {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    unsigned char h[208]{};
    input.read(reinterpret_cast<char*>(h), sizeof(h));
    const auto count = input.gcount();
    if (count < 8 || h[0] != 'I' || h[1] != 'D' || h[2] != 'S' || h[4] != 10 || h[5] || h[6] ||
        h[7]) {
        return false;
    }
    if (h[3] == 'Q') {
        return true; // separate sequence group, never standalone geometry
    }
    auto value = [&](int offset) {
        return std::uint32_t(h[offset]) | (std::uint32_t(h[offset + 1]) << 8) |
               (std::uint32_t(h[offset + 2]) << 16) | (std::uint32_t(h[offset + 3]) << 24);
    };
    return count == sizeof(h) && h[3] == 'T' && value(180) > 0 && value(204) == 0;
}
std::string importModel(AssetManager& assets,
                        const std::filesystem::path& source,
                        const std::string& folder,
                        bool publishRevision) {
    if (!importableModel(source.string())) {
        throw std::runtime_error(
            "Supported model formats: OBJ, glTF/GLB and Half-Life 1 GoldSrc MDL (IDST v10)");
    }
    if (!std::filesystem::is_regular_file(source) ||
        std::filesystem::file_size(source) > 256 * 1024 * 1024) {
        throw std::runtime_error("Model source must be a file smaller than 256 MiB");
    }
    if (modelCompanionFile(source)) {
        auto stem = source.stem().u8string();
        // Selecting a texture/group companion is common in model packs. Resolve
        // its main model instead of feeding a non-geometry file to Assimp.
        if (!stem.empty() && (stem.back() == 'T' || stem.back() == 't')) {
            stem.pop_back();
        } else if (stem.size() >= 2 && std::isdigit(static_cast<unsigned char>(stem.back())) &&
                   std::isdigit(static_cast<unsigned char>(stem[stem.size() - 2]))) {
            stem.resize(stem.size() - 2);
        }
        auto candidate = source.parent_path() / std::filesystem::u8path(stem + ".mdl");
        candidate =
            std::filesystem::u8path(GoldSrcIOSystem::existingPath(candidate.u8string().c_str()));
        if (candidate != source && std::filesystem::is_regular_file(candidate) &&
            !modelCompanionFile(candidate)) {
            return importModel(assets, candidate, folder, publishRevision);
        }
        throw std::runtime_error(
            "This MDL contains only textures/sequence data. Import its main GoldSrc model and keep all companion files beside it.");
    }
    // MDL is shared by unrelated formats. Accept only a main GoldSrc studio
    // model, never a Quake/Source model or a standalone sequence-group file.
    if (AssetManager::extension(source) == ".mdl") {
        std::ifstream in(source, std::ios::binary);
        unsigned char header[8]{};
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!in || header[0] != 'I' || header[1] != 'D' || header[2] != 'S' || header[3] != 'T' ||
            header[4] != 10 || header[5] || header[6] || header[7]) {
            throw std::runtime_error(
                "Expected a Half-Life 1 GoldSrc MDL (IDST version 10). Import the main model, not an external sequence group or Source-engine MDL.");
        }
    }
    Assimp::Importer importer;
    if (AssetManager::extension(source) == ".mdl") {
        importer.SetIOHandler(new GoldSrcIOSystem);
    }
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_ANIMATIONS, true);
    // Import render/skeleton data, not game-event and hitbox helper hierarchies.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_ANIMATION_EVENTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_BLEND_CONTROLLERS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_SEQUENCE_TRANSITIONS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_BONE_CONTROLLERS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_MISC_GLOBAL_INFO, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_HITBOXES, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_READ_ATTACHMENTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_MDL_HL1_TRANSFORM_COORD_SYSTEM, true);
    importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
    const auto* scene = importer.ReadFile(
        source.string(),
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_ValidateDataStructure |
            aiProcess_SortByPType | aiProcess_PopulateArmatureData);
    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error(
            std::string("Assimp import failed for ") + source.filename().string() + ": " +
            importer.GetErrorString() +
            (AssetManager::extension(source) == ".mdl"
                 ? " Import the main GoldSrc IDST v10 model and keep its T.mdl texture and numbered sequence-group companions beside it."
                 : ""));
    }
    if (scene->mNumAnimations > (AssetManager::extension(source) == ".mdl" ? 2048u : 256u)) {
        throw std::runtime_error("Too many model animations");
    }
    auto matrix = [](const aiMatrix4x4& m) {
        Json a = Json::array();
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                a.push_back(m[r][c]);
            }
        }
        return a;
    };
    Json nodes = Json::array();
    std::map<std::string, int> nodeIds;
    std::map<const aiNode*, int> nodePointers;
    std::vector<std::pair<unsigned, int>> meshNodes;
    std::function<void(const aiNode*, int)> visit = [&](const aiNode* node, int parent) {
        if (nodes.size() >= 4096) {
            throw std::runtime_error("Model exceeds 4096 skeleton nodes");
        }
        int id = int(nodes.size());
        nodeIds.emplace(node->mName.C_Str(), id);
        nodePointers[node] = id;
        aiVector3D scale, pos;
        aiQuaternion rot;
        node->mTransformation.Decompose(scale, rot, pos);
        nodes.push_back({{"name", node->mName.C_Str()},
                         {"parent", parent},
                         {"matrix", matrix(node->mTransformation)},
                         {"position", {pos.x, pos.y, pos.z}},
                         {"rotation", {rot.x, rot.y, rot.z, rot.w}},
                         {"scale", {scale.x, scale.y, scale.z}}});
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            meshNodes.push_back({node->mMeshes[i], id});
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i) {
            visit(node->mChildren[i], id);
        }
    };
    visit(scene->mRootNode, -1);
    Json animations = Json::array();
    std::size_t keyCount = 0;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        const auto* a = scene->mAnimations[i];
        double rate = a->mTicksPerSecond > 0 ? a->mTicksPerSecond : 25;
        double duration = a->mDuration / rate;
        if (AssetManager::extension(source) == ".mdl") {
            // Assimp 5.4's HL1 loader reports fps * frameCount as duration even
            // though key times are frame indices. Derive time from those keys,
            // retaining single-frame/zero-fps pose sequences as a nonzero clip.
            double lastTick = 0;
            for (unsigned c = 0; c < a->mNumChannels; ++c) {
                const auto* ch = a->mChannels[c];
                if (ch->mNumPositionKeys) {
                    lastTick =
                        std::max(lastTick, ch->mPositionKeys[ch->mNumPositionKeys - 1].mTime);
                }
                if (ch->mNumRotationKeys) {
                    lastTick =
                        std::max(lastTick, ch->mRotationKeys[ch->mNumRotationKeys - 1].mTime);
                }
                if (ch->mNumScalingKeys) {
                    lastTick = std::max(lastTick, ch->mScalingKeys[ch->mNumScalingKeys - 1].mTime);
                }
            }
            duration = std::max(1.0, lastTick) / rate;
        }
        if (!std::isfinite(duration) || duration <= 0 || duration > 86400) {
            throw std::runtime_error("Invalid model animation duration");
        }
        Json channels = Json::array();
        for (unsigned c = 0; c < a->mNumChannels; ++c) {
            const auto* ch = a->mChannels[c];
            auto found = nodeIds.find(ch->mNodeName.C_Str());
            if (found == nodeIds.end()) {
                throw std::runtime_error("Animation references missing node");
            }
            keyCount += ch->mNumPositionKeys + ch->mNumRotationKeys + ch->mNumScalingKeys;
            if (keyCount > 1000000) {
                throw std::runtime_error("Model animation key limit exceeded");
            }
            Json positions = Json::array(), rotations = Json::array(), scales = Json::array();
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                auto v = ch->mPositionKeys[k];
                positions.push_back({v.mTime / rate, v.mValue.x, v.mValue.y, v.mValue.z});
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                auto v = ch->mRotationKeys[k];
                rotations.push_back(
                    {v.mTime / rate, v.mValue.x, v.mValue.y, v.mValue.z, v.mValue.w});
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                auto v = ch->mScalingKeys[k];
                scales.push_back({v.mTime / rate, v.mValue.x, v.mValue.y, v.mValue.z});
            }
            channels.push_back({{"node", found->second},
                                {"positions", positions},
                                {"rotations", rotations},
                                {"scales", scales}});
        }
        if (a->mNumMeshChannels || a->mNumMorphMeshChannels) {
            throw std::runtime_error(
                "Morph/vertex animations are not supported; skeletal animations are supported");
        }
        animations.push_back({{"name", a->mName.length ? a->mName.C_Str() : "Animation"},
                              {"duration", duration},
                              {"channels", channels}});
    }
    const auto prefix = std::filesystem::path(folder);
    const auto stem = source.stem().string();
    bool richModel = scene->mNumAnimations || scene->mNumTextures || meshNodes.size() > 1;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        richModel |= scene->mMeshes[i]->HasBones();
    }
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        for (int type = aiTextureType_DIFFUSE; type <= aiTextureType_UNKNOWN; ++type) {
            richModel |= scene->mMaterials[i]->GetTextureCount(aiTextureType(type)) != 0;
        }
    }
    const std::string extension = richModel ? ".model" : ".mesh";
    auto path = (prefix / (stem + extension)).generic_string();
    for (unsigned i = 2; std::filesystem::exists(assets.resolve(path)); ++i) {
        path = (prefix / (stem + " (" + std::to_string(i) + ")" + extension)).generic_string();
    }
    // Keep generated files alongside the model, grouped by its manifest in the
    // browser rather than exposing a generated directory as a separate asset.
    std::vector<std::string> generated;
    Json subAssetNames = Json::object(), embeddedAssets = Json::object();
    std::vector<std::string> embeddedReferences;
    auto allocateEmbedded = [&](const std::string& leaf) {
        const auto reference = path + "#" + leaf;
        embeddedReferences.push_back(reference);
        subAssetNames[reference] = std::filesystem::path(leaf).stem().string();
        return reference;
    };
    auto allocateChild = [&](const std::string& leaf) {
        const auto name = std::filesystem::path(leaf).filename();
        const auto base = std::filesystem::path(path).stem().string() + "__" + name.stem().string();
        auto candidate = (prefix / (base + name.extension().string())).generic_string();
        for (unsigned i = 2; std::filesystem::exists(assets.resolve(candidate)); ++i) {
            candidate = (prefix / (base + "_" + std::to_string(i) + name.extension().string()))
                            .generic_string();
        }
        generated.push_back(candidate);
        subAssetNames[candidate] = name.stem().string();
        return candidate;
    };
    Json parts = Json::array();
    std::map<unsigned, std::string> materials;
    std::size_t count = 0;
    std::string warnings;
    try {
        auto texture = [&](const aiMaterial* material,
                           aiTextureType type,
                           const std::string& label) {
            aiString name;
            if (material->GetTexture(type, 0, &name) != AI_SUCCESS) {
                return std::string{};
            }
            const auto before = generated.size();
            try {
                if (const auto* embedded = scene->GetEmbeddedTexture(name.C_Str())) {
                    const std::string ext =
                        embedded->mHeight == 0 ? std::string(embedded->achFormatHint) : "tga";
                    const auto file = allocateChild(label + "." + (ext == "jpeg" ? "jpg" : ext));
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
                if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file) ||
                    std::filesystem::file_size(file) > 256ull * 1024 * 1024) {
                    throw std::runtime_error("Invalid or oversized model texture");
                }
                const auto copied = allocateChild(label + file.extension().string());
                std::filesystem::copy_file(file, assets.resolve(copied));
                return copied;
            } catch (const std::exception& error) {
                while (generated.size() > before) {
                    std::error_code ignored;
                    std::filesystem::remove(assets.resolve(generated.back()), ignored);
                    subAssetNames.erase(generated.back());
                    generated.pop_back();
                }
                warnings += std::string(name.C_Str()) + ": " + error.what() + "\n";
                return std::string{};
            }
        };
        for (unsigned instance = 0; instance < meshNodes.size(); ++instance) {
            const unsigned m = meshNodes[instance].first;
            const auto* mesh = scene->mMeshes[m];
            Json vertices = Json::array(), weights = Json::array(), bones = Json::array();
            std::vector<Json> vertexWeights(mesh->mNumVertices, Json::array());
            if (mesh->mNumBones > 4096) {
                throw std::runtime_error("Too many mesh bones");
            }
            for (unsigned b = 0; b < mesh->mNumBones; ++b) {
                const auto* bone = mesh->mBones[b];
                auto found = nodeIds.find(bone->mName.C_Str());
                if (found == nodeIds.end()) {
                    throw std::runtime_error("Missing bone node");
                }
                const int boneNode = bone->mNode && nodePointers.count(bone->mNode)
                                         ? nodePointers.at(bone->mNode)
                                         : found->second;
                bones.push_back({{"node", boneNode}, {"offset", matrix(bone->mOffsetMatrix)}});
                for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                    const auto value = bone->mWeights[w];
                    if (value.mVertexId >= mesh->mNumVertices || !std::isfinite(value.mWeight) ||
                        value.mWeight < 0) {
                        throw std::runtime_error("Invalid bone weight");
                    }
                    vertexWeights[value.mVertexId].push_back({b, value.mWeight});
                }
            }

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
                    weights.push_back(vertexWeights[index]);
                }
            }
            if (vertices.empty()) {
                continue;
            }
            if (parts.size() >= 1024) {
                throw std::runtime_error("Model exceeds 1024 mesh parts");
            }
            const auto meshPath = allocateEmbedded("Mesh_" + std::to_string(instance) + ".mesh");
            if (mesh->mName.length) {
                subAssetNames[meshPath] = mesh->mName.C_Str();
            }
            embeddedAssets[embeddedAssetName(meshPath)] =
                Json{{"format", "LegacyEngineMesh"},
                     {"version", 1},
                     {"settings", path},
                     {"vertices", std::move(vertices)},
                     {"weights", weights},
                     {"bones", bones},
                     {"node", meshNodes[instance].second}};
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
                const auto materialPath = allocateChild(label + ".mat");
                aiString materialName;
                if (sourceMaterial->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS &&
                    materialName.length) {
                    subAssetNames[materialPath] = materialName.C_Str();
                }
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
        std::set<std::string> animationMembers;
        for (unsigned i = 0; i < animations.size(); ++i) {
            // Keep source labels intact. Sanitize only the virtual filename, whose
            // separators and '#' are reserved by the embedded-reference syntax.
            auto name = animations[i]["name"].get<std::string>();
            if (name.empty()) {
                name = "Animation";
            }
            for (auto& c : name) {
                if (static_cast<unsigned char>(c) < 32 ||
                    std::string("/\\#:<>\"|?*").find(c) != std::string::npos) {
                    c = '_';
                }
            }
            if (name.size() > 180) {
                name.resize(180);
            }
            auto member = name + ".leanim";
            for (unsigned suffix = 2; !animationMembers.insert(member).second; ++suffix) {
                member = name + " (" + std::to_string(suffix) + ").leanim";
            }
            const auto animationPath = allocateEmbedded(member);
            subAssetNames[animationPath] = animations[i]["name"];
            embeddedAssets[embeddedAssetName(animationPath)] =
                Json{{"format", "LegacyEngineAnimation"},
                     {"version", 1},
                     {"duration", animations[i]["duration"]},
                     {"tracks", Json::array()},
                     {"model", path},
                     {"modelAnimation", i}};
        }
        // Publish one self-contained container; staging files never become assets.
        std::map<std::string, std::string> packedPaths;
        for (const auto& file : generated) {
            const auto reference =
                allocateEmbedded(std::filesystem::path(file).filename().string());
            packedPaths[file] = reference;
            if (AssetManager::extension(file) == ".mat") {
                embeddedAssets[embeddedAssetName(reference)] =
                    readJsonFile(assets.resolve(file), 64 * 1024);
            } else {
                std::ifstream input(assets.resolve(file), std::ios::binary);
                const auto size = std::filesystem::file_size(assets.resolve(file));
                if (size > 32 * 1024 * 1024) {
                    throw std::runtime_error("Packed texture exceeds 32 MiB");
                }
                std::string hex;
                hex.reserve(size * 2);
                constexpr char digits[] = "0123456789abcdef";
                char byte;
                while (input.get(byte)) {
                    const auto value = static_cast<unsigned char>(byte);
                    hex += digits[value >> 4];
                    hex += digits[value & 15];
                }
                if (!input.eof() || hex.size() != size * 2) {
                    throw std::runtime_error("Cannot pack model texture");
                }
                embeddedAssets[embeddedAssetName(reference)] =
                    Json{{"format", "LegacyEngineImage"}, {"hex", std::move(hex)}};
            }
            if (subAssetNames.contains(file)) {
                subAssetNames[reference] = subAssetNames[file];
                subAssetNames.erase(file);
            }
        }
        auto remapPacked = [&](auto&& self, Json& value) -> void {
            if (value.is_string()) {
                auto found = packedPaths.find(value.get_ref<const std::string&>());
                if (found != packedPaths.end()) {
                    value = found->second;
                }
            } else if (value.is_array() || value.is_object()) {
                for (auto& child : value) {
                    self(self, child);
                }
            }
        };
        remapPacked(remapPacked, parts);
        remapPacked(remapPacked, embeddedAssets);
        auto subAssets = std::vector<std::string>{};
        subAssets.insert(subAssets.end(), embeddedReferences.begin(), embeddedReferences.end());
        Json packed{{"format", "LegacyEngineMesh"},
                    {"version", 2},
                    {"importScale", 1},
                    {"subAssets", subAssets},
                    {"embeddedAssets", std::move(embeddedAssets)},
                    {"subAssetNames", subAssetNames},
                    {"parts", std::move(parts)},
                    {"warnings", warnings},
                    {"nodes", nodes},
                    {"animations", animations}};
        if (packed.dump().size() > 256 * 1024 * 1024) {
            throw std::runtime_error("Packed model exceeds 256 MiB");
        }
        writeJsonFile(assets.resolve(path), packed);
        for (const auto& file : generated) {
            std::error_code ignored;
            std::filesystem::remove(assets.resolve(file), ignored);
        }
        if (publishRevision) {
            ++modelAssetRevision;
        }
        return path;
    } catch (...) {
        std::error_code error;
        for (const auto& file : generated) {
            std::filesystem::remove(assets.resolve(file), error);
        }
        throw;
    }
}
