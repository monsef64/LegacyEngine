#pragma once
#include "editor/asset_picker.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_layout.hpp"
#include "editor/value_context.hpp"
#include "scene/scene_camera.hpp"
#include "terrain/terrain_tools.hpp"
#include <imgui.h>
namespace Terrains {
class Editor {
    Entity selected_{}, stroke_{};
    bool visible_ = true, enabled_ = false;
    int tool_ = 0, layer_ = 0, resolution_ = 1, brushShape_ = 0;
    float radius_ = 5, strength_ = 5, falloff_ = .5f, flatten_ = 0, size_ = 128, importScale_ = 50,
          importOffset_ = 0;
    std::string source_, heightmap_, error_;

    void drawBrushGrid() {
        ImGui::Text("Brush: %s", brushNames[brushShape_]);
        // Responsive horizontal/vertical thumbnail grid, scrollable on narrow panels.
        constexpr float cell = 54;
        int columns = std::clamp(int(ImGui::GetContentRegionAvail().x / (cell + 6)), 1, 5);
        if (ImGui::BeginChild("Brush shapes", ImVec2(0, 190), true)) {
            for (int i = 0; i < int(brushNames.size()); ++i) {
                ImGui::PushID(i);
                if (i % columns) {
                    ImGui::SameLine(0, 6);
                }
                auto origin = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("Brush", ImVec2(cell, cell))) {
                    brushShape_ = i;
                }
                bool hovered = ImGui::IsItemHovered();
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(
                    origin, {origin.x + cell, origin.y + cell}, IM_COL32(18, 21, 25, 255), 4);
                constexpr int pixels = 24;
                constexpr float spacing = (cell - 6) / pixels;
                for (int z = 0; z < pixels; ++z) {
                    for (int x = 0; x < pixels; ++x) {
                        float value =
                            brushMask(i, (x + .5f) / pixels * 2 - 1, (z + .5f) / pixels * 2 - 1);
                        int gray = int(value * 255);
                        auto a = ImVec2(origin.x + 3 + x * spacing, origin.y + 3 + z * spacing);
                        draw->AddRectFilled(
                            a, {a.x + spacing, a.y + spacing}, IM_COL32(gray, gray, gray, 255));
                    }
                }
                draw->AddRect(origin,
                              {origin.x + cell, origin.y + cell},
                              i == brushShape_ ? IM_COL32(255, 210, 65, 255)
                                               : IM_COL32(75, 80, 85, 255),
                              4,
                              0,
                              i == brushShape_ ? 2 : 1);
                if (hovered) {
                    ImGui::SetTooltip("%d. %s", i + 1, brushNames[i]);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

  public:
    bool active(World& world, Entity selected) const {
        return enabled_ && visible_ && world.has<Terrain>(selected);
    }
    void finish(World& world, Entity selected, EditorHistory& history) {
        if (world.alive(stroke_)) {
            if (auto* t = world.tryGet<Terrain>(stroke_)) {
                t->sculpting = false;
            }
            history.commit(world, selected);
        }
        stroke_ = {};
    }
    void draw(World& world,
              Entity selected,
              const AssetManager& assets,
              EditorHistory& history,
              bool editable,
              bool allowSave = true) {
        if (!world.alive(selected) || !world.has<Terrain>(selected)) {
            if (stroke_ != Entity{}) {
                finish(world, selected, history);
            }
            enabled_ = false;
            selected_ = selected;
            return;
        }
        if (selected != selected_) {
            finish(world, selected, history);
            selected_ = selected;
            visible_ = true;
            enabled_ = false;
        }
        if (openTools) {
            visible_ = true;
            openTools = false;
        }
        if (!editable) {
            finish(world, selected, history);
            enabled_ = false;
        }
        if (!visible_) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(360, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Terrain tools", &visible_)) {
            ImGui::BeginDisabled(!editable);
            auto& terrain = world.get<Terrain>(selected);
            EditorValues::Checkbox("Enable Scene brush", &enabled_);
            EditorValues::Combo("Tool", &tool_, "Raise\0Lower\0Smooth\0Flatten\0Paint\0");
            drawBrushGrid();
            EditorValues::DragFloat(
                "Brush radius", &radius_, .1f, .1f, 500, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            EditorValues::DragFloat(
                "Strength", &strength_, .1f, .01f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            EditorValues::SliderFloat("Falloff", &falloff_, 0, 1);
            if (tool_ == 3) {
                EditorValues::DragFloat("Flatten height", &flatten_, .1f, -10000, 10000);
            }
            if (tool_ == 4) {
                EditorValues::SliderInt("Paint layer", &layer_, 0, 3);
            }
            ImGui::TextWrapped(
                "Left-drag Scene to sculpt/paint. Alt navigation and right-drag remain camera controls. Release commits one undo step and updates collision.");
            ImGui::Separator();
            ImGui::TextWrapped(
                "For baked direct lighting: mark this object Static, set lights to Static, then bake from Windows > Lighting. Save afterward. Rebake after sculpting or moving terrain.");
            if (terrain.data && !terrain.data->lightmap.empty()) {
                ImGui::TextWrapped("Baked atlas: %s", terrain.data->lightmap.c_str());
                if (ImGui::Button("Clear terrain bake")) {
                    finish(world, selected, history);
                    history.commit(world, selected);
                    writable(terrain);
                    terrain.data->lightmap.clear();
                    terrain.data->revision = nextRevision();
                    history.commit(world, selected);
                }
            }
            ImGui::BeginDisabled(!allowSave);
            if (ImGui::Button("Save .terrain asset")) {
                try {
                    finish(world, selected, history);
                    history.commit(world, selected);
                    save(terrain, assets);
                    history.commit(world, selected);
                    error_.clear();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "%s%s",
                terrain.asset.empty() ? "Unsaved terrain" : terrain.asset.c_str(),
                terrain.data && terrain.data->revision != terrain.savedRevision ? " *" : "");
            assetPicker("Terrain asset", source_, &assets, [](const std::string& path) {
                return asset(path);
            });
            if (ImGui::Button("Load selected terrain") && !source_.empty()) {
                try {
                    finish(world, selected, history);
                    history.commit(world, selected);
                    Terrain loaded;
                    loaded.asset = source_;
                    load(loaded, assets);
                    terrain = std::move(loaded);
                    history.commit(world, selected);
                    error_.clear();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::TextDisabled("Load/Import/Flat replace data; Undo restores the prior sculpt.");
            if (terrain.data) {
                auto& data = *terrain.data;
                ImGui::Text("%d x %d height samples | %.1f m square",
                            data.resolution,
                            data.resolution,
                            data.size);
                for (int i = 0; i < 4; ++i) {
                    ImGui::PushID(i);
                    std::string title = "Material layer " + std::to_string(i);
                    if (ImGui::TreeNode(title.c_str())) {
                        auto edited = data.layers[i];
                        bool changed = assetPicker(
                            "Material asset", edited.material, &assets, [](const std::string& p) {
                                return materialAsset(p);
                            });
                        ImGui::TextWrapped(
                            "Material asset supplies albedo and normal maps; direct map fields are used when no material is selected. Tint, strength and tiling multiply the material settings.");
                        changed |= assetPicker(
                            "Albedo", edited.texture, &assets, [](const std::string& p) {
                                return materialTexturePath(p);
                            });
                        changed |= assetPicker(
                            "Normal map", edited.normalMap, &assets, [](const std::string& p) {
                                return materialTexturePath(p);
                            });
                        changed |= EditorValues::SliderFloat(
                            "Normal strength", &edited.normalStrength, 0, 4);
                        changed |= EditorValues::ColorEdit3("Tint", &edited.color.x);
                        changed |= EditorValues::DragFloat("Tiles across terrain",
                                                           &edited.tiling,
                                                           .1f,
                                                           .01f,
                                                           1000,
                                                           "%.1f",
                                                           ImGuiSliderFlags_AlwaysClamp);
                        if (changed) {
                            writable(terrain);
                            terrain.data->layers[i] = edited;
                            terrain.data->revision = nextRevision();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            if (ImGui::CollapsingHeader("Heightmap import")) {
                assetPicker("Heightmap", heightmap_, &assets, [](const std::string& p) {
                    return materialTexturePath(p);
                });
                EditorValues::DragFloat("Height range",
                                        &importScale_,
                                        .5f,
                                        0,
                                        10000,
                                        "%.1f m",
                                        ImGuiSliderFlags_AlwaysClamp);
                EditorValues::DragFloat("Height offset", &importOffset_, .1f, -10000, 10000);
                if (ImGui::Button("Import heightmap") && !heightmap_.empty()) {
                    try {
                        finish(world, selected, history);
                        history.commit(world, selected);
                        importHeightmap(terrain, assets, heightmap_, importScale_, importOffset_);
                        history.commit(world, selected);
                        error_.clear();
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                ImGui::TextWrapped(
                    "8/16-bit images, up to 4096 pixels. Bilinearly resampled to this terrain grid. Image top maps to terrain Z=0.");
            }
            if (ImGui::CollapsingHeader("New flat terrain")) {
                EditorValues::Combo("Resolution", &resolution_, "65\0 129\0 257\0");
                EditorValues::DragFloat(
                    "Terrain size", &size_, 1, 1, 10000, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::Button("Replace with flat terrain")) {
                    finish(world, selected, history);
                    history.commit(world, selected);
                    auto d = std::make_shared<Data>();
                    d->resolution = (1 << (resolution_ + 6)) + 1;
                    d->size = size_;
                    d->heights.assign(d->resolution * d->resolution, 0);
                    d->weights.assign(d->resolution * d->resolution, {1, 0, 0, 0});
                    invalidate(*d);
                    terrain.data = d;
                    history.commit(world, selected);
                }
            }
            if (!error_.empty()) {
                ImGui::TextWrapped("%s", error_.c_str());
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
        if (!visible_ || !enabled_) {
            finish(world, selected, history);
        }
    }
    // Editor overlay: projected onto the actual heightfield, not a flat plane.
    void preview(World& world,
                 Entity selected,
                 const Ray* ray,
                 const Mat4& vp,
                 const EditorRect& rect,
                 ImDrawList* draw) {
        if (!draw || !ray || !active(world, selected)) {
            return;
        }
        const auto& terrain = world.get<Terrain>(selected);
        const auto* t = world.tryGet<Transform>(selected);
        if (!terrain.data || !t || std::abs(t->scale.x * t->scale.y * t->scale.z) < 1e-8f) {
            return;
        }
        Quat inverse{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
        auto origin = rotateVector(inverse, ray->origin - t->position),
             direction = rotateVector(inverse, ray->direction);
        origin = {origin.x / t->scale.x, origin.y / t->scale.y, origin.z / t->scale.z};
        direction = {direction.x / t->scale.x, direction.y / t->scale.y, direction.z / t->scale.z};
        Vec3 hit;
        const auto& d = *terrain.data;
        if (!intersect(d, origin, direction, hit)) {
            return;
        }
        auto project = [&](float x, float z, ImVec2& out) {
            if (x < 0 || z < 0 || x > d.size || z > d.size) {
                return false;
            }
            float fx = x / d.size * (d.resolution - 1), fz = z / d.size * (d.resolution - 1);
            int ix = std::min(int(fx), d.resolution - 2), iz = std::min(int(fz), d.resolution - 2);
            float u = fx - ix, v = fz - iz;
            auto a = point(d, ix, iz), b = point(d, ix + 1, iz), c = point(d, ix, iz + 1),
                 e = point(d, ix + 1, iz + 1);
            float y = u + v <= 1 ? a.y * (1 - u - v) + b.y * u + c.y * v
                                 : b.y * (1 - v) + c.y * (1 - u) + e.y * (u + v - 1);
            auto worldPoint = t->position + rotateVector(t->rotation, Vec3{x, y, z} * t->scale);
            float w = vp.m[3] * worldPoint.x + vp.m[7] * worldPoint.y + vp.m[11] * worldPoint.z +
                      vp.m[15];
            if (w <= 1e-5f) {
                return false;
            }
            auto p = projectCameraPoint(vp, worldPoint);
            if (p.z < -1 || p.z > 1) {
                return false;
            }
            out = {rect.x + (p.x + 1) * .5f * rect.width, rect.y + (1 - p.y) * .5f * rect.height};
            return true;
        };
        draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
        auto ring = [&](float radius, ImU32 color, float width) {
            ImVec2 previous;
            bool valid = false;
            for (int i = 0; i <= 128; ++i) {
                float angle = i * (2 * kPi / 128);
                ImVec2 p;
                bool next =
                    project(hit.x + std::cos(angle) * radius, hit.z + std::sin(angle) * radius, p);
                if (valid && next) {
                    draw->AddLine(previous, p, color, width);
                }
                previous = p;
                valid = next;
            }
        };
        ring(radius_, IM_COL32(10, 10, 10, 220), 4);
        ring(radius_, IM_COL32(255, 210, 65, 255), 2);
        if (brushShape_ == 0 && falloff_ > 0) {
            ring(radius_ * std::sqrt(1 - std::pow(.5f, 1 / (falloff_ * 4))),
                 IM_COL32(255, 230, 130, 190),
                 1);
        }
        ImVec2 center;
        if (project(hit.x, hit.z, center)) {
            draw->AddLine({center.x - 5, center.y}, {center.x + 5, center.y}, IM_COL32_WHITE, 1.5f);
            draw->AddLine({center.x, center.y - 5}, {center.x, center.y + 5}, IM_COL32_WHITE, 1.5f);
        }
        draw->PopClipRect();
    }
    void stroke(World& world,
                Entity selected,
                EditorHistory& history,
                const Ray* ray,
                bool down,
                float dt) {
        if (!down) {
            if (stroke_ != Entity{}) {
                finish(world, selected, history);
            }
            return;
        }
        if (!ray || !active(world, selected)) {
            return;
        }
        auto& terrain = world.get<Terrain>(selected);
        const auto* t = world.tryGet<Transform>(selected);
        if (!terrain.data || !t || std::abs(t->scale.x * t->scale.y * t->scale.z) < 1e-8f) {
            return;
        }
        Quat inverse{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
        Vec3 origin = rotateVector(inverse, ray->origin - t->position),
             direction = rotateVector(inverse, ray->direction);
        origin = {origin.x / t->scale.x, origin.y / t->scale.y, origin.z / t->scale.z};
        direction = {direction.x / t->scale.x, direction.y / t->scale.y, direction.z / t->scale.z};
        Vec3 hit;
        if (!intersect(*terrain.data, origin, direction, hit)) {
            return;
        }
        if (stroke_ == Entity{}) {
            history.commit(world, selected);
            stroke_ = selected;
            terrain.sculpting = true;
        }
        brush(terrain,
              hit,
              tool_,
              radius_,
              strength_,
              falloff_,
              flatten_,
              layer_,
              std::clamp(dt, 0.f, .05f),
              brushShape_);
    }
};
} // namespace Terrains
