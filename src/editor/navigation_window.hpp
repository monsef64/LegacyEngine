#pragma once
#include "editor/lightmapping_window.hpp"
#include "navigation/navigation.hpp"
#include <set>
#include <sstream>

class NavigationWindow {
    Navigation::Settings bakeSettings_;
    std::filesystem::path root_;
    std::string level_, signature_, status_;
    std::set<std::string> dependencies_;
    double nextValidation_ = 0;
    unsigned revision_ = 0;
    struct TerrainHash {
        unsigned revision = ~0u;
        std::uint64_t value = 0;
    };
    std::map<unsigned, TerrainHash> terrainHashes_;
    static std::string key(const std::string& text) {
        std::uint64_t value = 14695981039346656037ull;
        for (unsigned char c : text) {
            value ^= c;
            value *= 1099511628211ull;
        }
        return std::to_string(value);
    }
    static bool eligible(World& world, Entity e, const Transform& t) {
        if (!t.isStatic || !isActive(world, e) || world.has<Water>(e) || world.has<River>(e) ||
            world.has<Camera>(e) || world.has<NavMeshObstacle>(e)) {
            return false;
        }
        if (const auto* collider = world.tryGet<Collider>(e); collider && collider->isTrigger) {
            return false;
        }
        for (unsigned depth = 0; world.alive(e); ++depth, e = getParent(world, e)) {
            if (depth >= 128 || world.has<Animation>(e) || world.has<Animator>(e) ||
                world.has<NavMeshObstacle>(e) ||
                (world.has<RigidBody>(e) && world.get<RigidBody>(e).mass > 0) ||
                world.has<CharacterController>(e) || world.has<NavMeshAgent>(e) ||
                world.has<BoneAttachment>(e)) {
                return false;
            }
        }
        return std::abs(t.scale.x * t.scale.y * t.scale.z) > 1e-10f;
    }
    std::string signature(World& world, const AssetManager& assets) {
        std::vector<std::string> objects;
        world.eachReadOnly<Transform>([&](Entity e, const Transform& t) {
            if (!eligible(world, e, t)) {
                return;
            }
            const auto* terrain = world.tryGet<Terrain>(e);
            const auto* mesh = world.tryGet<MeshRenderer>(e);
            const auto* collider = world.tryGet<Collider>(e);
            if (!terrain && !mesh && !collider) {
                return;
            }
            std::ostringstream out;
            out << std::hexfloat << t.position.x << ',' << t.position.y << ',' << t.position.z
                << ',' << t.rotation.x << ',' << t.rotation.y << ',' << t.rotation.z << ','
                << t.rotation.w << ',' << t.scale.x << ',' << t.scale.y << ',' << t.scale.z << ':';
            if (terrain && terrain->data) {
                const auto& d = *terrain->data;
                auto& hash = terrainHashes_[d.identity];
                if (hash.revision != d.shapeRevision) {
                    hash.revision = d.shapeRevision;
                    hash.value = 14695981039346656037ull;
                    const auto* bytes = reinterpret_cast<const unsigned char*>(d.heights.data());
                    for (std::size_t i = 0; i < d.heights.size() * sizeof(float); ++i) {
                        hash.value ^= bytes[i];
                        hash.value *= 1099511628211ull;
                    }
                }
                out << "T" << d.size << ':' << d.resolution << ':' << hash.value;
            } else if (mesh) {
                out << "M" << mesh->model.size() << ':' << mesh->model << ':'
                    << (mesh->model.empty() ? mesh->mesh.id : 0);
            } else {
                out << "C" << int(collider->shape) << ':' << collider->isTrigger << ':'
                    << collider->size.x << ',' << collider->size.y << ',' << collider->size.z << ':'
                    << collider->center.x << ',' << collider->center.y << ',' << collider->center.z;
            }
            objects.push_back(out.str());
        });
        std::sort(objects.begin(), objects.end());
        Json result{{"objects", objects}, {"files", Json::object()}};
        for (const auto& path : dependencies_) {
            const auto file = assets.resolve(path);
            result["files"][path] = {
                std::filesystem::last_write_time(file).time_since_epoch().count(),
                std::filesystem::file_size(file)};
        }
        return result.dump();
    }

  public:
    bool visible = false, showMesh = false;
    Navigation::System system;
    void draw(World& world,
              const AssetManager& assets,
              const ProjectIO::Meshes& meshes,
              const std::string& level,
              bool editAllowed) {
        const auto file =
            assets.root.parent_path() / "Settings" / ("Navigation_" + key(level) + ".json");
        if (root_ != assets.root || level_ != level) {
            root_ = assets.root;
            level_ = level;
            signature_.clear();
            dependencies_.clear();
            terrainHashes_.clear();
            system.clear();
            status_.clear();
            nextValidation_ = 0;
            revision_ = modelAssetRevision;
            try {
                if (std::filesystem::is_regular_file(file)) {
                    signature_ = system.load(file);
                    bakeSettings_ = system.settings;
                    const auto doc = parseJson(signature_);
                    for (const auto& item : doc.at("files").items()) {
                        dependencies_.insert(item.key());
                    }
                    system.valid = signature(world, assets) == signature_;
                    if (!system.valid) {
                        status_ = "Saved navmesh is stale; bake again.";
                    }
                }
            } catch (const std::exception& e) {
                system.clear();
                status_ = e.what();
            }
        }
        // Poll even when hidden. Static changes invalidate the snapshot; dynamic
        // motion does not. No per-frame canonicalization or terrain height hashing.
        if (ImGui::GetTime() >= nextValidation_ || revision_ != modelAssetRevision) {
            nextValidation_ = ImGui::GetTime() + 1;
            revision_ = modelAssetRevision;
            if (!signature_.empty()) {
                try {
                    system.valid = signature(world, assets) == signature_;
                } catch (const std::exception& e) {
                    system.valid = false;
                    status_ = e.what();
                }
            }
        }
        if (!visible) {
            return;
        }
        if (!ImGui::Begin("Navigation", &visible)) {
            ImGui::End();
            return;
        }
        ImGui::TextWrapped(
            "Recast navmesh: mark floors, obstacles and terrains Static. Agent origins are at their feet. Dynamic bodies, water and animated hierarchies are excluded. Changes to static geometry require rebaking.");
        auto& s = bakeSettings_;
        ImGui::BeginDisabled(!editAllowed);
        ImGui::SliderFloat("Cell size", &s.cellSize, .1f, 5);
        ImGui::SliderFloat("Cell height", &s.cellHeight, .05f, 1);
        ImGui::SliderFloat("Agent radius", &s.radius, .05f, 5);
        ImGui::SliderFloat("Agent height", &s.height, .2f, 10);
        ImGui::SliderFloat("Max climb", &s.climb, 0, 2);
        ImGui::SliderFloat("Max slope", &s.slope, 0, 80);
        if (ImGui::Button("Bake Navmesh")) {
            try {
                world.each<Terrain>([&](Entity, Terrain& t) { Terrains::load(t, assets); });
                Navigation::Geometry geometry;
                dependencies_.clear();
                std::map<std::string, Json> cache;
                world.eachReadOnly<Transform>([&](Entity e, const Transform& t) {
                    if (!eligible(world, e, t)) {
                        return;
                    }
                    MeshData mesh;
                    if (const auto* terrain = world.tryGet<Terrain>(e); terrain && terrain->data) {
                        mesh = Terrains::bakeMesh(*terrain->data);
                    } else if (const auto* r = world.tryGet<MeshRenderer>(e)) {
                        if (!r->model.empty()) {
                            mesh = loadModelAsset(assets, r->model, 0, -1, 0, &cache);
                        } else {
                            mesh = Lightmapping::geometry({e, t, *r, {}}, assets, meshes);
                        }
                    } else if (const auto* c = world.tryGet<Collider>(e); c && !c->isTrigger) {
                        if (c->shape != ColliderShape::Box) {
                            throw std::runtime_error(
                                "Collider-only navigation geometry currently supports boxes; use a visible mesh for other shapes");
                        }
                        mesh = makeBox(c->size);
                        for (auto& v : mesh.vertices) {
                            v.position = v.position + c->center;
                        }
                    }
                    geometry.append(mesh, t);
                });
                for (const auto& entry : cache) {
                    dependencies_.insert(entry.first);
                }
                system.bake(geometry, s);
                signature_ = signature(world, assets);
                system.save(file, signature_);
                status_ = system.status;
                showMesh = true;
            } catch (const std::exception& e) {
                system.clear();
                signature_.clear();
                status_ = e.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Navmesh")) {
            try {
                std::filesystem::remove(file);
                system.clear();
                signature_.clear();
                status_ = "Cleared navmesh";
            } catch (const std::exception& e) {
                status_ = e.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("Show navmesh in Scene", &showMesh);
        ImGui::Text("Navigation: %s", system.valid ? "Ready" : "Missing / stale");
        ImGui::TextWrapped(
            "Bake is synchronous and bounded to 2 million input triangles / 4 million XZ cells. Agent dimensions apply to all agents. No dynamic obstacle avoidance, crowds or off-mesh links. Use Transform-only agents, not physics/camera movement drivers.");
        if (!status_.empty()) {
            ImGui::TextWrapped("%s", status_.c_str());
        }
        ImGui::End();
    }
};
