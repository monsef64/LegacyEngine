#pragma once
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <map>

namespace Lightmapping {
// Order/ID independent so save/reload and undo can recover a matching bake.
// Excludes atlas assignments and renderer-generated GPU handles for imported meshes.
inline std::string staticState(World& world) {
    std::vector<std::string> records;
    auto vec = [](Vec3 v) { return Json::array({v.x, v.y, v.z}); };
    world.eachReadOnly<Transform>([&](Entity e, const Transform& t) {
        if (!t.isStatic) {
            return;
        }
        Json pose = {vec(t.position),
                     Json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}),
                     vec(t.scale),
                     isActive(world, e)};
        if (const auto* mesh = world.tryGet<MeshRenderer>(e)) {
            records.push_back(
                Json{{"pose", pose},
                     {"model", mesh->model},
                     {"primitive", mesh->model.empty() ? mesh->mesh.id : 0u},
                     {"animated", world.has<Animation>(e)},
                     {"dynamicBody", world.has<RigidBody>(e) && world.get<RigidBody>(e).mass > 0},
                     {"character", world.has<CharacterController>(e)},
                     {"camera", world.has<Camera>(e)}}
                    .dump());
        }
        auto light = [&](const char* type, const LightSettings& l) {
            records.push_back(Json{
                {"type", type},
                {"pose", pose},
                {"color", vec(l.color)},
                {"intensity", l.intensity},
                {"enabled", l.enabled},
                {"shadows", l.shadows},
                {"range", l.range},
                {"inner", l.innerAngle},
                {"outer",
                 l.outerAngle}}.dump());
        };
        if (auto* l = world.tryGet<DirectionalLight>(e)) {
            light("directional", *l);
        }
        if (auto* l = world.tryGet<PointLight>(e)) {
            light("point", *l);
        }
        if (auto* l = world.tryGet<SpotLight>(e)) {
            light("spot", *l);
        }
    });
    std::sort(records.begin(), records.end());
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& record : records) {
        for (unsigned char c : record) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        hash ^= 0xff;
        hash *= 1099511628211ull;
    }
    return std::to_string(hash);
}
class ValidationCache {
    struct Entry {
        std::chrono::steady_clock::time_point checked{};
        std::string state;
        std::map<std::string, int> atlases;
    };
    std::map<std::string, Entry> entries_;
    std::filesystem::path root_;

  public:
    bool valid(const AssetManager& assets, const MeshRenderer& mesh, const std::string& state) {
        if (mesh.lightmap.empty() || mesh.lightmapGrid <= 0) {
            return false;
        }
        if (root_ != assets.root) {
            entries_.clear();
            root_ = assets.root;
        }
        const auto parent = std::filesystem::path(mesh.lightmap).parent_path();
        const auto key = parent.generic_string();
        if (!entries_.count(key) && entries_.size() >= 256) {
            entries_.erase(entries_.begin());
        }
        auto& entry = entries_[key];
        const auto now = std::chrono::steady_clock::now();
        if (entry.checked.time_since_epoch().count() == 0 ||
            now - entry.checked > std::chrono::seconds(1)) {
            entry.checked = now;
            entry.state.clear();
            entry.atlases.clear();
            try {
                const auto manifest = readJsonFile(
                    assets.resolve((parent / "bake.json").generic_string()), 4 * 1024 * 1024);
                if (manifest.at("format") == "LegacyEngineLightmapBake" &&
                    manifest.at("version") == 2) {
                    const auto state = manifest.at("staticState").get<std::string>();
                    std::map<std::string, int> atlases;
                    for (const auto& object : manifest.at("objects")) {
                        atlases.emplace(object.at("atlas").get<std::string>(),
                                        object.at("grid").get<int>());
                    }
                    entry.state = state;
                    entry.atlases = std::move(atlases);
                }
            } catch (const std::exception&) {
                entry.state.clear();
                entry.atlases.clear();
            }
        }
        const auto atlas = entry.atlases.find(mesh.lightmap);
        return entry.state == state && atlas != entry.atlases.end() &&
               atlas->second == mesh.lightmapGrid;
    }
};
} // namespace Lightmapping
