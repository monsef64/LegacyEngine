#pragma once
#include "assets/assets.hpp"
#include "materials/material_io.hpp"
#include "project/json_io.hpp"
#include <cstring>
#include <map>

namespace Lightmapping {
// Order/ID independent so save/reload and undo can recover a matching bake.
// Excludes atlas assignments and renderer-generated GPU handles for imported meshes.
inline std::string staticState(World& world, const AssetManager& assets) {
    static thread_local MaterialAssetCache materials;
    static thread_local std::filesystem::path root;
    if (root != assets.root) {
        root = assets.root;
        materials.clear();
    }
    // Hash numeric values directly: JSON DOM construction/dump here used to run
    // for every static object, every viewport, every frame.
    struct Hash {
        std::uint64_t value = 14695981039346656037ull;
        void scalar(std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                value ^= (v & 255);
                value *= 1099511628211ull;
                v >>= 8;
            }
        }
        void number(float f) {
            if (f == 0) {
                f = 0; // normalize negative zero across save/reload
            }
            std::uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            scalar(bits);
        }
        void vec(Vec3 v) {
            number(v.x);
            number(v.y);
            number(v.z);
        }
        void text(const std::string& s) {
            scalar(s.size());
            for (unsigned char c : s) {
                value ^= c;
                value *= 1099511628211ull;
            }
        }
    };
    static thread_local std::vector<std::uint64_t> records;
    records.clear();
    world.eachReadOnly<Transform>([&](Entity e, const Transform& t) {
        if (!t.isStatic) {
            return;
        }
        // Match the previous fingerprint's fields, excluding runtime GPU handles
        // for model assets. Record sorting keeps it independent of entity IDs.
        Hash pose;
        pose.vec(t.position);
        pose.number(t.rotation.x);
        pose.number(t.rotation.y);
        pose.number(t.rotation.z);
        pose.number(t.rotation.w);
        pose.vec(t.scale);
        pose.scalar(isActive(world, e));
        if (const auto* mesh = world.tryGet<MeshRenderer>(e)) {
            auto h = pose;
            h.scalar(1);
            h.text(mesh->model);
            h.scalar(mesh->model.empty() ? mesh->mesh.id : 0u);
            try {
                MaterialProperties material;
                material.color = mesh->color;
                if (!mesh->material.empty()) {
                    material = materials.get(assets, mesh->material);
                }
                material =
                    applyMaterialOverrides(material, mesh->materialValues, mesh->materialOverrides);
                h.scalar(1);
                h.vec(material.color);
            } catch (const std::exception&) {
                h.scalar(0);
            }
            h.scalar(world.has<Animation>(e));
            h.scalar(world.has<RigidBody>(e) && world.get<RigidBody>(e).mass > 0);
            h.scalar(world.has<CharacterController>(e));
            h.scalar(world.has<Camera>(e));
            records.push_back(h.value);
        }
        auto light = [&](unsigned type, const LightSettings& l) {
            auto h = pose;
            h.scalar(type);
            h.vec(l.color);
            h.number(l.intensity);
            h.scalar(l.enabled);
            h.scalar(l.shadows);
            h.number(l.range);
            h.number(l.innerAngle);
            h.number(l.outerAngle);
            records.push_back(h.value);
        };
        if (auto* l = world.tryGet<DirectionalLight>(e)) {
            light(2, *l);
        }
        if (auto* l = world.tryGet<PointLight>(e)) {
            light(3, *l);
        }
        if (auto* l = world.tryGet<SpotLight>(e)) {
            light(4, *l);
        }
    });
    std::sort(records.begin(), records.end());
    Hash scene;
    scene.scalar(records.size());
    for (auto record : records) {
        scene.scalar(record);
    }
    return "numeric1:" + std::to_string(scene.value);
}
class ValidationCache {
    struct Entry {
        std::chrono::steady_clock::time_point checked{};
        std::string state;
        std::map<std::string, int> atlases;
        std::map<std::string, std::string> indirect;
    };
    std::map<std::string, Entry> entries_;
    std::filesystem::path root_;

  public:
    bool valid(const AssetManager& assets, const MeshRenderer& mesh, const std::string& /*state*/) {
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
            entry.indirect.clear();
            try {
                const auto manifest = readJsonFile(
                    assets.resolve((parent / "bake.json").generic_string()), 4 * 1024 * 1024);
                if (manifest.at("format") == "LegacyEngineLightmapBake" &&
                    (manifest.at("version") == 3 || manifest.at("version") == 4)) {
                    // A bake is an authored snapshot, not a live scene cache. Keep
                    // its shadows/GI until an explicit rebake or atlas reassignment.
                    // Moving static geometry must not discard every receiver's bake.
                    std::map<std::string, int> atlases;
                    for (const auto& object : manifest.at("objects")) {
                        atlases.emplace(object.at("atlas").get<std::string>(),
                                        object.at("grid").get<int>());
                    }
                    for (const auto& object : manifest.at("objects")) {
                        entry.indirect.emplace(object.at("atlas").get<std::string>(),
                                               object.at("indirect").get<std::string>());
                    }
                    entry.state = "persistent";
                    entry.atlases = std::move(atlases);
                }
            } catch (const std::exception&) {
                entry.state.clear();
                entry.atlases.clear();
                entry.indirect.clear();
            }
        }
        const auto atlas = entry.atlases.find(mesh.lightmap);
        return !entry.state.empty() && atlas != entry.atlases.end() &&
               atlas->second == mesh.lightmapGrid;
    }
    std::string
    indirect(const AssetManager& assets, const MeshRenderer& mesh, const std::string& state) {
        if (!valid(assets, mesh, state)) {
            return {};
        }
        const auto& paths =
            entries_.at(std::filesystem::path(mesh.lightmap).parent_path().generic_string())
                .indirect;
        const auto it = paths.find(mesh.lightmap);
        return it == paths.end() ? std::string{} : it->second;
    }
};
} // namespace Lightmapping
