#pragma once
#include "project/project_io.hpp"
#include "rendering/renderer.hpp"
#include <imgui.h>

class OcclusionWindow {
    std::filesystem::path root_;
    std::string level_, status_;
    static std::string key(const std::string& level) {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : level) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return std::to_string(hash);
    }
    static Json vec(Vec3 p) {
        return {p.x, p.y, p.z};
    }
    static Vec3 vector(const Json& j) {
        if (!j.is_array() || j.size() != 3) {
            throw std::runtime_error("Invalid occlusion vertex");
        }
        Vec3 p{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
        for (float v : {p.x, p.y, p.z}) {
            if (!std::isfinite(v) || std::abs(v) > 1e7f) {
                throw std::runtime_error("Invalid occlusion coordinate");
            }
        }
        return p;
    }

  public:
    bool visible = false, visualizeSelectedCamera = false;
    std::size_t previewCulled = 0, previewTested = 0;
    bool previewValid = false;
    void draw(World& world,
              Renderer& renderer,
              const AssetManager& assets,
              const ProjectIO::Meshes& meshes,
              const std::string& level,
              bool editAllowed) {
        const auto file =
            assets.root.parent_path() / "Settings" / ("Occlusion_" + key(level) + ".json");
        auto& bake = renderer.occlusion;
        if (root_ != assets.root || level_ != level) {
            root_ = assets.root;
            level_ = level;
            renderer.setAssetRoot(assets.root);
            bake.clear();
            status_.clear();
            try {
                if (std::filesystem::is_regular_file(file)) {
                    const auto doc = readJsonFile(file, 4 * 1024 * 1024);
                    if (doc.at("version") != 1 || doc.at("level") != level ||
                        !doc.at("triangles").is_array() || doc.at("triangles").size() > 2048) {
                        throw std::runtime_error("Invalid occlusion bake");
                    }
                    std::vector<Occlusion::Triangle> triangles;
                    for (const auto& t : doc.at("triangles")) {
                        triangles.push_back({vector(t.at("a")),
                                             vector(t.at("b")),
                                             vector(t.at("c")),
                                             {t.at("index").get<std::uint32_t>(),
                                              t.at("generation").get<std::uint32_t>()}});
                    }
                    bake.signature = doc.at("signature").get<std::string>();
                    bake.triangles = std::move(triangles);
                    bake.enabled = true;
                    status_ =
                        "Loaded bake; stale data automatically bypassed. Re-bake if scene identity changed.";
                }
            } catch (const std::exception& e) {
                bake.clear();
                status_ = e.what();
            }
        }
        if (!visible) {
            return;
        }
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::min(500.f, available.x), std::min(640.f, available.y)},
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(
            {std::min(320.f, available.x), std::min(260.f, available.y)}, available);
        if (!ImGui::Begin("Occlusion Culling", &visible)) {
            ImGui::End();
            return;
        }
        ImGui::TextWrapped(
            "Bake static opaque mesh and terrain surface triangles. Only objects marked Static are tested. Dynamic objects, water, animated hierarchies, transparent materials and material overrides are excluded conservatively.");
        ImGui::Checkbox("Enable baked occlusion", &bake.enabled);
        ImGui::Checkbox("Visualize selected camera", &visualizeSelectedCamera);
        if (visualizeSelectedCamera) {
            ImGui::TextWrapped(
                "Select a Camera in the hierarchy. Scene overlay: red bounds = occluded, green = retained in the camera frustum; cyan triangles = usable baked occluders. The Camera preview uses normal baked culling.");
            ImGui::Text("Selected camera: %zu culled / %zu tested", previewCulled, previewTested);
            if (!previewValid) {
                ImGui::TextWrapped(
                    "Select a perspective camera and enable a valid bake to visualize culling.");
            }
        }
        ImGui::Text("Baked triangles: %zu / 2048", bake.triangles.size());
        ImGui::Text("Last view: %zu culled / %zu tested", bake.culled, bake.tested);
        if (bake.enabled && !bake.valid) {
            ImGui::TextDisabled(
                "Bypassed: stale/empty bake, pending assets, or orthographic view.");
        }
        ImGui::BeginDisabled(!editAllowed || assets.root.empty());
        if (ImGui::Button("Bake static objects")) {
            try {
                world.each<Terrain>(
                    [&](Entity, Terrain& terrain) { Terrains::load(terrain, assets); });
                bake.generate(
                    world,
                    modelAssetRevision,
                    [&](const MeshRenderer& r) {
                        if (!r.model.empty()) {
                            return loadModelAsset(assets, r.model);
                        }
                        if (r.mesh.id == meshes[1].id) {
                            return makePlane(16, 16);
                        }
                        if (r.mesh.id == meshes[2].id) {
                            return makeBox({1, 1, 1});
                        }
                        if (r.mesh.id == meshes[3].id) {
                            return makeSphere(.5f);
                        }
                        if (r.mesh.id == meshes[4].id) {
                            return makeCapsule(.5f, 1);
                        }
                        if (r.mesh.id == meshes[5].id) {
                            return makeCone(.5f, 1);
                        }
                        return MeshData{}; // editor lines/unknown procedural geometry never occlude
                    },
                    [&](const MeshRenderer& r) { return renderer.opaqueOcclusionMaterial(r); });
                Json doc{{"version", 1},
                         {"level", level},
                         {"signature", bake.signature},
                         {"triangles", Json::array()}};
                for (const auto& t : bake.triangles) {
                    doc["triangles"].push_back({{"a", vec(t.a)},
                                                {"b", vec(t.b)},
                                                {"c", vec(t.c)},
                                                {"index", t.owner.index},
                                                {"generation", t.owner.generation}});
                }
                writeJsonFile(file, doc);
                status_ = bake.status + " Saved to " + file.filename().string();
            } catch (const std::exception& e) {
                bake.clear();
                status_ = e.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear bake")) {
            try {
                std::filesystem::remove(file);
                bake.clear();
                status_ = "Bake cleared.";
            } catch (const std::exception& e) {
                status_ = e.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "Full bounds must be behind a single baked triangle. Holes remain open; small triangles may yield little culling. Near-clipped/back-facing occluders and orthographic cameras are bypassed. Shadow casters remain visible to shadow passes. Static meshes use full detail while a valid bake is active.");
        if (!status_.empty()) {
            ImGui::TextWrapped("%s", status_.c_str());
        }
        ImGui::End();
    }
};
