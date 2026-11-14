#pragma once
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_clip.hpp"
#include "editor/asset_picker.hpp"
#include "editor/component_clipboard.hpp"
#include "editor/editor_layout.hpp"
#include "editor/level_objects.hpp"
#include "editor/rotation_editor.hpp"
#include "editor/transform_edit.hpp"
#include "materials/material_io.hpp"
#include "project/layers.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/script_exports.hpp"
#include <ImGuizmo.h>
#include <cmath>
#include <cstdio>
#include <cstring>

inline bool editVector(const char* label,
                       Vec3& value,
                       float speed = 0.05f,
                       float minimum = -1000000,
                       float maximum = 1000000) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    float v[]{value.x, value.y, value.z};
    bool changed = false;
    const char* axes[]{"X", "Y", "Z"};
    const float axisWidth = std::max(20.0f, (ImGui::GetContentRegionAvail().x - 90) / 3);
    for (int i = 0; i < 3; ++i) {
        if (i) {
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(axes[i]);
        ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(axisWidth);
        changed |= ImGui::DragFloat(
            "##axis", &v[i], speed, minimum, maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
    }
    ImGui::PopID();
    if (!changed) {
        return false;
    }
    for (float n : v) {
        if (!std::isfinite(n)) {
            return false;
        }
    }
    value = {v[0], v[1], v[2]};
    return true;
}
template <class T> inline bool componentHeader(World& world, Entity entity, const char* name) {
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(.16f, .23f, .31f, 1));
    const bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
    const auto header = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(header.x, header.y + 3),
                                              ImVec2(header.x + 3, end.y - 3),
                                              ImGui::GetColorU32(ImGuiCol_CheckMark),
                                              2);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    bool remove = false;
    if (ImGui::BeginPopupContextItem(name)) {
        if (ImGui::MenuItem("Copy values")) {
            componentClipboard.copy<T>(world, entity);
        }
        if (ImGui::MenuItem("Paste values", nullptr, false, componentClipboard.matches<T>())) {
            componentClipboard.paste<T>(world, entity);
        }
        ImGui::Separator();
        bool allowed = true;
        if constexpr (std::is_same_v<T, Transform>) {
            allowed = !world.has<Camera>(entity) && !world.has<AudioListener>(entity) &&
                      !world.has<DirectionalLight>(entity) && !world.has<PointLight>(entity) &&
                      !world.has<SpotLight>(entity) && !world.has<CharacterController>(entity) &&
                      !world.has<FPSCamera>(entity);
        }
        if constexpr (std::is_same_v<T, Collider> || std::is_same_v<T, RigidBody>) {
            allowed = !world.has<CharacterController>(entity);
        }
        if constexpr (std::is_same_v<T, Camera>) {
            allowed = !world.has<FPSCamera>(entity);
        }
        remove = ImGui::MenuItem("Remove component", nullptr, false, allowed);
        if (!allowed) {
            ImGui::TextDisabled(
                "Remove dependent Camera / listener / lights / controller / FPSCamera first");
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
        int type = -1;
        if constexpr (std::is_same_v<T, Transform>) {
            type = 0;
        }
        if constexpr (std::is_same_v<T, RigidBody>) {
            type = 1;
        }
        if constexpr (std::is_same_v<T, Collider>) {
            type = 2;
        }
        if constexpr (std::is_same_v<T, MeshRenderer>) {
            type = 3;
        }
        if constexpr (std::is_same_v<T, LuaScript>) {
            type = 5;
        }
        if constexpr (std::is_same_v<T, Camera>) {
            type = 6;
        }
        if constexpr (std::is_same_v<T, AudioSource>) {
            type = 7;
        }
        if constexpr (std::is_same_v<T, AudioListener>) {
            type = 8;
        }
        if constexpr (std::is_same_v<T, DirectionalLight>) {
            type = 9;
        }
        if constexpr (std::is_same_v<T, PointLight>) {
            type = 10;
        }
        if constexpr (std::is_same_v<T, SpotLight>) {
            type = 11;
        }
        if constexpr (std::is_same_v<T, Skybox>) {
            type = 12;
        }
        if constexpr (std::is_same_v<T, CharacterController>) {
            type = 13;
        }
        if constexpr (std::is_same_v<T, FPSCamera>) {
            type = 14;
        }
        if constexpr (std::is_same_v<T, Raycast>) {
            type = 15;
        }
        EditorComponentPayload payload{entity, type};
        ImGui::SetDragDropPayload("EDITOR_COMPONENT", &payload, sizeof(payload));
        ImGui::TextUnformatted(name);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("EDITOR_COMPONENT")) {
            const auto source = *static_cast<const EditorComponentPayload*>(payload->Data);
            int target = -1;
            const char* names[]{"Transform",
                                "RigidBody",
                                "Collider",
                                "MeshRenderer",
                                "ObjectName",
                                "LegacyScripts",
                                "Camera",
                                "AudioSource",
                                "AudioListener",
                                "DirectionalLight",
                                "PointLight",
                                "SpotLight",
                                "Skybox",
                                "CharacterController",
                                "FPSCamera",
                                "Raycast"};
            for (int i = 0; i < 16; ++i) {
                if (!std::strcmp(name, names[i])) {
                    target = i;
                }
            }
            if (source.entity == entity && target >= 0 && source.type != target) {
                auto& order =
                    world.has<InspectorOrder>(entity)
                        ? world.get<InspectorOrder>(entity)
                        : world.add(
                              entity,
                              InspectorOrder{{0, 3, 2, 1, 6, 9, 10, 11, 12, 8, 7, 5, 13, 14, 15}});
                auto from = std::find(order.types.begin(), order.types.end(), source.type);
                if (from != order.types.end()) {
                    order.types.erase(from);
                }
                auto to = std::find(order.types.begin(), order.types.end(), target);
                order.types.insert(to, source.type);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (remove) {
        world.remove<T>(entity);
        return false;
    }
    return open;
}
inline void drawScriptExports(World& world,
                              std::vector<ScriptField>& fields,
                              const AssetManager* assets = nullptr) {
    const char* componentTypes[]{"Transform",
                                 "RigidBody",
                                 "Collider",
                                 "MeshRenderer",
                                 "ObjectName",
                                 "LuaScript",
                                 "Camera",
                                 "AudioSource",
                                 "AudioListener",
                                 "DirectionalLight",
                                 "PointLight",
                                 "SpotLight",
                                 "Skybox",
                                 "CharacterController",
                                 "FPSCamera",
                                 "Raycast"};
    for (auto& field : fields) {
        ImGui::PushID(field.name.c_str());
        ImGui::Text("%s (%s)", field.name.c_str(), field.type.c_str());
        if (field.type == "number") {
            ImGui::DragFloat("##value",
                             &field.number,
                             .1f,
                             -1000000,
                             1000000,
                             "%.3f",
                             ImGuiSliderFlags_AlwaysClamp);
        } else if (field.type == "boolean") {
            ImGui::Checkbox("##value", &field.boolean);
        } else if (field.type == "string") {
            char value[4097];
            std::snprintf(value, sizeof(value), "%s", field.text.c_str());
            if (ImGui::InputText("##value", value, sizeof(value))) {
                field.text = value;
            }
        } else {
            const bool asset = exportAssetType(field.type);
            const auto* name = world.tryGet<ObjectName>(field.entity);
            const std::string value = asset
                                          ? (field.text.empty() ? "None (drop asset)" : field.text)
                                      : exportHasComponent(world, field.entity, field.type)
                                          ? (name ? name->value : "GameObject")
                                          : "None (drop object / component)";
            if (asset) {
                assetPicker("##Asset", field.text, assets, [&](const std::string& path) {
                    return field.type == "AudioClip" ? audioAsset(path)
                           : field.type == "Texture"
                               ? (materialTexturePath(path) || skyboxHDRPath(path))
                               : true;
                });
            } else {
                ImGui::Button(value.c_str(),
                              ImVec2(std::max(40.0f, ImGui::GetContentRegionAvail().x - 36), 0));
            }
            if (ImGui::BeginDragDropTarget()) {
                if (asset) {
                    if (auto* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        std::string path = static_cast<const char*>(p->Data);
                        if ((field.type != "AudioClip" || audioAsset(path)) &&
                            (field.type != "Texture" ||
                             (!path.empty() &&
                              (materialTexturePath(path) || skyboxHDRPath(path))))) {
                            field.text = path;
                        }
                    }
                } else {
                    if (auto* p = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                        Entity target = *static_cast<const Entity*>(p->Data);
                        if (exportHasComponent(world, target, field.type)) {
                            field.entity = target;
                        }
                    }
                    if (auto* p = ImGui::AcceptDragDropPayload("EDITOR_COMPONENT")) {
                        auto target = *static_cast<const EditorComponentPayload*>(p->Data);
                        if (target.type >= 0 && target.type < 16 &&
                            (field.type == "GameObject" ||
                             field.type == componentTypes[target.type]) &&
                            exportHasComponent(world, target.entity, field.type)) {
                            field.entity = target.entity;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                field.entity = {};
                field.text.clear();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clear assignment");
            }
        }
        ImGui::PopID();
    }
}
inline void drawMaterialAssetInspector(const AssetManager& assets, const std::string& path) {
    ImGui::TextWrapped("Material: %s", path.c_str());
    ImGui::Separator();
    try {
        auto edited = loadMaterial(assets, path);
        bool changed = false;
        changed |= ImGui::Combo(
            "Shader", &edited.shader, "Diffuse\0Diffuse Bumped\0Diffuse Transparent\0");
        changed |= ImGui::ColorEdit3("Color", &edited.color.x);
        auto texture = [&](const char* label, std::string& value) {
            bool result = assetPicker(label, value, &assets, [](const std::string& file) {
                return materialTexturePath(file);
            });
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* drop = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string file = static_cast<const char*>(drop->Data);
                    if (materialTexturePath(file)) {
                        value = file;
                        result = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            return result;
        };
        changed |= texture("Texture", edited.texture);
        if (edited.shader == 1) {
            changed |= texture("Normal map", edited.normalMap);
            changed |= ImGui::SliderFloat("Normal strength", &edited.normalStrength, 0, 4);
        }
        float tiling[]{edited.tilingX, edited.tilingY};
        if (ImGui::DragFloat2("UV tiling", tiling, .05f, -1000, 1000)) {
            edited.tilingX = tiling[0];
            edited.tilingY = tiling[1];
            changed = true;
        }
        changed |= ImGui::SliderFloat("Specular", &edited.specular, 0, 1);
        ImGui::TextWrapped(
            "Shared asset: edits are saved immediately and affect all objects using this material.");
        if (changed && validMaterial(edited)) {
            saveMaterial(assets, path, edited);
        }
    } catch (const std::exception& error) {
        ImGui::TextWrapped("%s", error.what());
    }
}

inline void drawInspector(World& world,
                          Entity selected,
                          const EditorRect& rect,
                          const MeshHandle* meshes,
                          bool editingGizmo,
                          const AssetManager* assets = nullptr,
                          int meshCount = 4,
                          const std::string& selectedAsset = {}) {
    if (dockedEditor && !panelOpen[1]) {
        return;
    }
    placeEditorPanel(rect);
    if (beginEditorPanel("Inspector", fixedPanelFlags)) {
        if (assets && materialAsset(selectedAsset)) {
            ImGui::BeginDisabled(editingGizmo);
            drawMaterialAssetInspector(*assets, selectedAsset);
            ImGui::EndDisabled();
        } else if (!world.alive(selected)) {
            ImGui::TextDisabled("Select a GameObject in Hierarchy or Scene.");
        } else {
            ImGui::PushID(static_cast<int>(selected.index));
            ImGui::BeginDisabled(editingGizmo);
            const float available = ImGui::GetContentRegionAvail().x;
            const float checkWidth = ImGui::GetFrameHeight();
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float layerWidth = std::max(40.f, std::min(150.f, available * .32f));
            auto* objectName = world.tryGet<ObjectName>(selected);
            char name[257];
            std::snprintf(name, sizeof(name), "%s", objectName ? objectName->value.c_str() : "");
            ImGui::SetNextItemWidth(std::max(30.f, available - layerWidth - checkWidth - 2 * gap));
            if (ImGui::InputText("##Name", name, sizeof(name))) {
                world.add(selected, ObjectName{name});
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("GameObject name");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(layerWidth);
            int layer = LayerSettings::effective(world, selected);
            if (ImGui::BeginCombo("##Layer", LayerSettings::names[layer].c_str())) {
                for (int i = 0; i < 32; ++i) {
                    if (!LayerSettings::names[i].empty() &&
                        ImGui::Selectable(LayerSettings::names[i].c_str(), layer == i)) {
                        world.add(selected, ObjectLayer{i});
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("GameObject layer");
            }
            ImGui::SameLine();
            bool objectActive =
                !world.has<ObjectActivity>(selected) || world.get<ObjectActivity>(selected).enabled;
            if (ImGui::Checkbox("##Active", &objectActive)) {
                setObjectActive(world, selected, objectActive);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Active");
            }
            const Entity parent = getParent(world, selected);
            const auto* parentName = world.tryGet<ObjectName>(parent);
            if (ImGui::BeginCombo("Parent",
                                  parent == Entity{} ? "None"
                                  : parentName       ? parentName->value.c_str()
                                                     : "Object")) {
                if (ImGui::Selectable("None", parent == Entity{})) {
                    setParent(world, selected, {});
                }
                for (const auto& entry : levelObjects(world)) {
                    if (canParent(world, selected, entry.entity)) {
                        ImGui::PushID(static_cast<int>(entry.entity.index));
                        if (ImGui::Selectable(entry.name.c_str(), parent == entry.entity)) {
                            setParent(world, selected, entry.entity);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
            auto ordered =
                world.has<InspectorOrder>(selected)
                    ? world.get<InspectorOrder>(selected).types
                    : std::vector<int>{0, 3, 2, 1, 6, 9, 10, 11, 12, 8, 7, 5, 13, 14, 15};
            for (int id : {0, 3, 2, 1, 6, 9, 10, 11, 12, 8, 7, 5, 13, 14, 15}) {
                if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
                    ordered.push_back(id);
                }
            }
            for (int componentID : ordered) {
                if (auto* t = world.tryGet<Transform>(selected);
                    componentID == 0 && t &&
                    componentHeader<Transform>(world, selected, "Transform")) {
                    const bool child = getParent(world, selected) != Entity{};
                    auto edited = localTransform(world, selected);
                    bool transformChanged =
                        editVector(child ? "Local position" : "Position", edited.position);
                    static RotationEditorState rotationState;
                    rotationState.sync(selected, edited.rotation);
                    Vec3 rotation = rotationState.angles;
                    if (editVector("Rotation (deg)", rotation, .5f, -360, 360)) {
                        edited.rotation = rotationState.apply(rotation);
                        transformChanged = true;
                    }
                    const auto* c = world.tryGet<Collider>(selected);
                    if (c && c->shape != ColliderShape::Box) {
                        float size = edited.scale.x;
                        if (ImGui::DragFloat("Uniform scale",
                                             &size,
                                             0.02f,
                                             0.05f,
                                             100,
                                             "%.3f",
                                             ImGuiSliderFlags_AlwaysClamp) &&
                            std::isfinite(size)) {
                            edited.scale = {size, size, size};
                            transformChanged = true;
                        }
                    } else {
                        transformChanged |= editVector("Scale", edited.scale, 0.02f, 0.05f, 100);
                    }
                    if (transformChanged) {
                        setLocalTransform(world, selected, edited);
                    }
                }
                if (auto* r = world.tryGet<MeshRenderer>(selected);
                    componentID == 3 && r &&
                    componentHeader<MeshRenderer>(world, selected, "MeshRenderer")) {
                    const char* names[]{"Cube", "Sphere", "Capsule", "Cone", "Plane"};
                    int current = -1;
                    for (int i = 0; i < std::min(meshCount, 5); ++i) {
                        if (r->mesh.id == meshes[i].id) {
                            current = i;
                        }
                    }
                    if (ImGui::BeginCombo("Mesh", current < 0 ? "Custom" : names[current])) {
                        for (int i = 0; i < std::min(meshCount, 5); ++i) {
                            if (ImGui::Selectable(names[i], current == i)) {
                                r->mesh = meshes[i];
                                if (i != 0) {
                                    if (auto* t = world.tryGet<Transform>(selected)) {
                                        const float scale =
                                            std::clamp(std::max({std::abs(t->scale.x),
                                                                 std::abs(t->scale.y),
                                                                 std::abs(t->scale.z)}),
                                                       0.05f,
                                                       100.0f);
                                        t->scale = {scale, scale, scale};
                                    }
                                }
                                if (auto* c = world.tryGet<Collider>(selected)) {
                                    c->shape =
                                        i == 4 ? ColliderShape::Box : static_cast<ColliderShape>(i);
                                    c->size = i == 4 ? Vec3{16, .1f, 16} : Vec3{1, 1, 1};
                                    c->center = i == 4 ? Vec3{0, -.05f, 0} : Vec3{};
                                    c->radius = 0.5f;
                                    c->height = 1;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (assets) {
                        static std::string materialError;
                        try {
                            if (assetPicker(
                                    "Material", r->material, assets, [](const std::string& file) {
                                        return materialAsset(file);
                                    })) {
                                r->materialOverrides = 0;
                            }
                            if (ImGui::BeginDragDropTarget()) {
                                if (const auto* payload =
                                        ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                    std::string path = static_cast<const char*>(payload->Data);
                                    if (materialAsset(path)) {
                                        r->material = path;
                                        r->materialOverrides = 0;
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            auto create = [&]() {
                                auto path = createMaterial(*assets);
                                MaterialProperties p;
                                p.color = r->color;
                                p.specular = r->specular;
                                saveMaterial(*assets, path, p);
                                r->material = path;
                                r->materialOverrides = 0;
                            };
                            bool createRequested = false;
                            if (ImGui::BeginPopupContextItem("Material slot actions")) {
                                createRequested = ImGui::MenuItem("Create Material");
                                ImGui::EndPopup();
                            }
                            if (createRequested) {
                                create();
                            }
                            if (r->material.empty() && ImGui::SmallButton("Create Material")) {
                                create();
                            }
                            if (!r->material.empty()) {
                                auto edited = loadMaterial(*assets, r->material);
                                if (ImGui::TreeNodeEx("Material properties",
                                                      ImGuiTreeNodeFlags_DefaultOpen)) {
                                    bool changed = false;
                                    changed |= ImGui::Combo(
                                        "Shader",
                                        &edited.shader,
                                        "Diffuse\0Diffuse Bumped\0Diffuse Transparent\0");
                                    changed |= ImGui::ColorEdit3("Material color", &edited.color.x);
                                    auto textureField = [&](const char* label, std::string& path) {
                                        ImGui::PushID(label);
                                        bool editedTexture = false;
                                        editedTexture |= assetPicker(
                                            label, path, assets, [](const std::string& file) {
                                                return materialTexturePath(file);
                                            });
                                        if (ImGui::BeginDragDropTarget()) {
                                            if (const auto* payload =
                                                    ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                                std::string value =
                                                    static_cast<const char*>(payload->Data);
                                                if (!value.empty() && materialTexturePath(value)) {
                                                    path = value;
                                                    editedTexture = true;
                                                }
                                            }
                                            ImGui::EndDragDropTarget();
                                        }
                                        ImGui::PopID();
                                        return editedTexture;
                                    };
                                    changed |= textureField("Texture", edited.texture);
                                    if (edited.shader == 1) {
                                        changed |= textureField("Normal map", edited.normalMap);
                                        changed |= ImGui::SliderFloat(
                                            "Normal strength", &edited.normalStrength, 0, 4);
                                    }
                                    float tiling[]{edited.tilingX, edited.tilingY};
                                    if (ImGui::DragFloat2("UV tiling", tiling, .05f, -1000, 1000)) {
                                        edited.tilingX = tiling[0];
                                        edited.tilingY = tiling[1];
                                        changed = true;
                                    }
                                    changed |= ImGui::SliderFloat(
                                        "Material specular", &edited.specular, 0, 1);
                                    ImGui::TextDisabled(
                                        "Shared asset: edits affect all meshes using this material.");
                                    if (changed && validMaterial(edited)) {
                                        try {
                                            saveMaterial(*assets, r->material, edited);
                                            materialError.clear();
                                        } catch (const std::exception& e) {
                                            materialError = e.what();
                                        }
                                    }
                                    if (r->materialOverrides) {
                                        ImGui::TextDisabled("This mesh has instance overrides.");
                                        if (ImGui::SmallButton("Reset material overrides")) {
                                            r->materialOverrides = 0;
                                        }
                                    }
                                    ImGui::TreePop();
                                }
                            }
                        } catch (const std::exception& e) {
                            materialError = e.what();
                        }
                        if (!materialError.empty()) {
                            ImGui::TextWrapped("%s", materialError.c_str());
                        }
                    }
                    if (r->material.empty()) {
                        float color[]{r->color.x, r->color.y, r->color.z};
                        if (ImGui::ColorEdit3("Color", color)) {
                            r->color = {color[0], color[1], color[2]};
                        }
                        ImGui::Checkbox("Checkerboard", &r->checkerboard);
                        ImGui::SliderFloat("Specular", &r->specular, 0, 1);
                    }
                }
                if (auto* c = world.tryGet<Collider>(selected);
                    componentID == 2 && c &&
                    componentHeader<Collider>(world, selected, "Collider")) {
                    const char* names[]{"Box", "Sphere", "Capsule", "Cone"};
                    int shape = static_cast<int>(c->shape);
                    if (ImGui::Combo("Collider shape", &shape, names, 4)) {
                        c->shape = static_cast<ColliderShape>(shape);
                    }
                    editVector("Center", c->center);
                    if (c->shape == ColliderShape::Box) {
                        editVector("Size", c->size, 0.02f, 0.01f, 10000);
                    } else {
                        ImGui::DragFloat("Radius",
                                         &c->radius,
                                         0.02f,
                                         0.01f,
                                         10000,
                                         "%.3f",
                                         ImGuiSliderFlags_AlwaysClamp);
                        if (c->shape != ColliderShape::Sphere) {
                            ImGui::DragFloat("Height",
                                             &c->height,
                                             0.02f,
                                             0.01f,
                                             10000,
                                             "%.3f",
                                             ImGuiSliderFlags_AlwaysClamp);
                        }
                    }
                    ImGui::Checkbox("Is trigger", &c->isTrigger);
                    ImGui::SliderFloat("Friction", &c->friction, 0, 1);
                    ImGui::SliderFloat("Bounciness", &c->bounciness, 0, 1);
                }
                if (auto* rb = world.tryGet<RigidBody>(selected);
                    componentID == 1 && rb &&
                    componentHeader<RigidBody>(world, selected, "RigidBody")) {
                    ImGui::DragFloat(
                        "Mass", &rb->mass, 0.1f, 0, 1000000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::Checkbox("Use gravity", &rb->useGravity);
                    ImGui::Checkbox("Kinematic", &rb->isKinematic);
                    ImGui::DragFloat(
                        "Drag", &rb->drag, .01f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Angular drag",
                                     &rb->angularDrag,
                                     .01f,
                                     0,
                                     100,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    editVector("Linear velocity", rb->linearVelocity);
                    editVector("Angular velocity", rb->angularVelocity);
                }
                if (auto* camera = world.tryGet<Camera>(selected);
                    componentID == 6 && camera &&
                    componentHeader<Camera>(world, selected, "Camera")) {
                    Camera edited = *camera;
                    bool active = camera->active;
                    if (ImGui::Checkbox("Active camera", &active)) {
                        if (active) {
                            activateCamera(world, selected);
                        } else {
                            camera->active = false;
                        }
                    }
                    edited.active = camera->active;
                    ImGui::Checkbox("Orthographic", &edited.orthographic);
                    ImGui::DragFloat("Field of view (degrees)", &edited.fieldOfView, .1f, 1, 178);
                    ImGui::DragFloat(
                        "Orthographic size", &edited.orthographicSize, .1f, .01f, 1000000);
                    ImGui::DragFloat(
                        "Near clip", &edited.nearClip, .01f, .001f, edited.farClip - .001f);
                    ImGui::DragFloat(
                        "Far clip", &edited.farClip, 1, edited.nearClip + .001f, 1000000);
                    float viewport[]{edited.viewportX,
                                     edited.viewportY,
                                     edited.viewportWidth,
                                     edited.viewportHeight};
                    if (ImGui::DragFloat4("Viewport", viewport, .005f, 0, 1)) {
                        edited.viewportX = viewport[0];
                        edited.viewportY = viewport[1];
                        edited.viewportWidth = viewport[2];
                        edited.viewportHeight = viewport[3];
                    }
                    if (validCamera(edited)) {
                        *camera = edited;
                    }
                    if (ImGui::Button("Clear camera target")) {
                        camera->tracking = false;
                        camera->following = false;
                        camera->targetEntity = {};
                    }
                }
                auto lightInspector = [&](auto tag, const char* title, int kind) {
                    using T = decltype(tag);
                    auto* light = world.tryGet<T>(selected);
                    if (!light || !componentHeader<T>(world, selected, title)) {
                        return;
                    }
                    T edited = *light;
                    ImGui::PushID(title);
                    ImGui::Checkbox("Enabled", &edited.enabled);
                    ImGui::ColorEdit3("Color", &edited.color.x);
                    ImGui::DragFloat("Intensity", &edited.intensity, .05f, 0, 1000);
                    if (kind != 0) {
                        ImGui::DragFloat("Range", &edited.range, .1f, .1f, 10000);
                    }
                    if (kind == 2) {
                        ImGui::DragFloat(
                            "Inner angle", &edited.innerAngle, .1f, 0, edited.outerAngle - .1f);
                        ImGui::DragFloat(
                            "Outer angle", &edited.outerAngle, .1f, edited.innerAngle + .1f, 85);
                    }
                    ImGui::Checkbox("Cast dynamic shadows", &edited.shadows);
                    ImGui::Combo("Shadow quality",
                                 &edited.shadowQuality,
                                 "Off\0Low (256)\0Medium (512)\0High (1024)\0");
                    if (kind == 0) {
                        ImGui::DragFloat("Shadow coverage", &edited.shadowDistance, .5f, 1, 1000);
                    }
                    ImGui::DragFloat("Shadow bias", &edited.shadowBias, .0001f, 0, .05f, "%.4f");
                    ImGui::TextWrapped(
                        "Emission follows local -Z. Shadow maps use a shared 12-layer budget; a point light uses six layers.");
                    if (validLight(edited)) {
                        *light = edited;
                    }
                    ImGui::PopID();
                };
                if (auto* sky = world.tryGet<Skybox>(selected);
                    componentID == 12 && sky &&
                    componentHeader<Skybox>(world, selected, "Skybox")) {
                    ImGui::Checkbox("Skybox enabled", &sky->enabled);
                    ImGui::Combo("Skybox type", &sky->type, "Cubemap asset\0HDR panorama\0");
                    ImGui::ColorEdit3("Skybox color", &sky->color.x);
                    auto slot = [&](const char* label, std::string& path, bool hdr) {
                        ImGui::PushID(label);
                        assetPicker(label, path, assets, [&](const std::string& file) {
                            return hdr ? skyboxHDRPath(file) : cubemapAsset(file);
                        });
                        if (ImGui::BeginDragDropTarget()) {
                            if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                std::string value = static_cast<const char*>(payload->Data);
                                if (!value.empty() &&
                                    (hdr ? skyboxHDRPath(value) : cubemapAsset(value))) {
                                    path = value;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopID();
                    };
                    if (sky->type == 1) {
                        slot("HDR panorama", sky->hdr, true);
                    } else {
                        slot("Cubemap asset", sky->cubemap, false);
                    }
                }
                if (componentID == 9) {
                    lightInspector(DirectionalLight{}, "DirectionalLight", 0);
                }
                if (componentID == 10) {
                    lightInspector(PointLight{}, "PointLight", 1);
                }
                if (componentID == 11) {
                    lightInspector(SpotLight{}, "SpotLight", 2);
                }
                if (auto* listener = world.tryGet<AudioListener>(selected);
                    componentID == 8 && listener &&
                    componentHeader<AudioListener>(world, selected, "AudioListener")) {
                    ImGui::Checkbox("Listener enabled", &listener->enabled);
                    ImGui::TextWrapped(
                        "Receives audio at this object's world position. With multiple enabled listeners, the lowest entity ID is used.");
                }
                if (auto* a = world.tryGet<AudioSource>(selected);
                    componentID == 7 && a &&
                    componentHeader<AudioSource>(world, selected, "AudioSource")) {
                    assetPicker("Audio clip", a->clip, assets, [](const std::string& path) {
                        return audioAsset(path);
                    });
                    if (ImGui::BeginDragDropTarget()) {
                        if (auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                            std::string path = static_cast<const char*>(payload->Data);
                            if (audioAsset(path)) {
                                a->clip = path;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SliderFloat(
                        "Volume", &a->volume, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat(
                        "Pitch", &a->pitch, .1f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat("Pan", &a->pan, -1, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Max Distance",
                                     &a->distance,
                                     .1f,
                                     0,
                                     10000,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat(
                        "Spatial Blend", &a->blend, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::Checkbox("Looping", &a->looping);
                    ImGui::Checkbox("Mute", &a->muted);
                    ImGui::Checkbox("Play on start", &a->playOnStart);
                    ImGui::TextUnformatted(a->isPlaying()                           ? "Playing"
                                           : a->state == AudioSource::State::Paused ? "Paused"
                                                                                    : "Stopped");
                }
                if (auto* scripts = world.tryGet<LuaScript>(selected);
                    componentID == 5 && scripts &&
                    componentHeader<LuaScript>(world, selected, "LegacyScripts")) {
                    int remove = -1;
                    auto row = [&](std::string& path,
                                   bool& enabled,
                                   std::vector<ScriptField>& fields,
                                   int index) {
                        ImGui::PushID(index);
                        ImGui::SeparatorText(("Script " + std::to_string(index + 1)).c_str());
                        if (ImGui::BeginPopupContextItem("Script actions")) {
                            if (ImGui::MenuItem("Remove script")) {
                                remove = index;
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::Checkbox("Enabled", &enabled);
                        assetPicker("Script", path, assets, [](const std::string& file) {
                            return AssetManager::script(file);
                        });
                        if (ImGui::BeginDragDropTarget()) {
                            if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                std::string file = static_cast<const char*>(payload->Data);
                                if (AssetManager::script(file)) {
                                    path = file;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (assets && !path.empty()) {
                            try {
                                syncScriptExports(assets->readScript(path), fields);
                            } catch (const std::exception& error) {
                                ImGui::TextWrapped("%s", error.what());
                            }
                        }
                        drawScriptExports(world, fields, assets);
                        ImGui::PopID();
                    };
                    row(scripts->path, scripts->enabled, scripts->fields, 0);
                    for (std::size_t i = 0; i < scripts->additional.size(); ++i) {
                        row(scripts->additional[i].path,
                            scripts->additional[i].enabled,
                            scripts->additional[i].fields,
                            static_cast<int>(i + 1));
                    }
                    if (remove == 0) {
                        if (scripts->additional.empty()) {
                            world.remove<LuaScript>(selected);
                        } else {
                            auto next = scripts->additional.front();
                            scripts->path = next.path;
                            scripts->enabled = next.enabled;
                            scripts->fields = next.fields;
                            scripts->additional.erase(scripts->additional.begin());
                        }
                    } else if (remove > 0) {
                        scripts->additional.erase(scripts->additional.begin() + remove - 1);
                    }
                }
                if (auto* c = world.tryGet<CharacterController>(selected);
                    componentID == 13 && c &&
                    componentHeader<CharacterController>(world, selected, "CharacterController")) {
                    ImGui::DragFloat(
                        "Move speed", &c->speed, .1f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Sprint multiplier",
                                     &c->sprintMultiplier,
                                     .05f,
                                     1,
                                     10,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Jump speed",
                                     &c->jumpSpeed,
                                     .1f,
                                     0,
                                     100,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::Checkbox("Can move", &c->canMove);
                    ImGui::SameLine();
                    ImGui::Checkbox("Can jump", &c->canJump);
                    ImGui::SameLine();
                    ImGui::Checkbox("Can sprint", &c->canSprint);
                }
                if (auto* f = world.tryGet<FPSCamera>(selected);
                    componentID == 14 && f &&
                    componentHeader<FPSCamera>(world, selected, "FPSCamera")) {
                    ImGui::Checkbox("Freecam", &f->isFreecam);
                    ImGui::Checkbox("Mouse look", &f->mouseLook);
                    ImGui::DragFloat("Look sensitivity",
                                     &f->sensitivity,
                                     .01f,
                                     .01f,
                                     5,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Freecam speed",
                                     &f->speed,
                                     .1f,
                                     0,
                                     100,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextWrapped(
                        "Inherits Camera settings. An unparented freecam uses WASD, Q/E while Game input is focused. Mouse look requires a locked cursor.");
                }
                if (auto* ray = world.tryGet<Raycast>(selected);
                    componentID == 15 && ray &&
                    componentHeader<Raycast>(world, selected, "Raycast")) {
                    ImGui::Combo("Query type", &ray->type, "Line\0LineOfSight\0Sphere\0");
                    editVector("Origin offset", ray->offset);
                    ImGui::DragFloat("Distance",
                                     &ray->distance,
                                     .1f,
                                     .01f,
                                     10000,
                                     "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    if (ray->type == 2) {
                        ImGui::DragFloat("Radius",
                                         &ray->radius,
                                         .1f,
                                         .01f,
                                         10000,
                                         "%.2f",
                                         ImGuiSliderFlags_AlwaysClamp);
                    }
                    if (ray->type == 1) {
                        ImGui::SliderFloat("Fan angle", &ray->angle, 1, 360);
                        ImGui::SliderInt("Fan samples", &ray->samples, 2, 181);
                    }
                    ImGui::Checkbox("Include triggers", &ray->includeTriggers);
                    if (ImGui::TreeNode("Query layers")) {
                        for (int i = 0; i < 32; ++i) {
                            if (!LayerSettings::names[i].empty()) {
                                bool enabled = ray->layerMask & (1u << i);
                                if (ImGui::Checkbox(LayerSettings::names[i].c_str(), &enabled)) {
                                    if (enabled) {
                                        ray->layerMask |= 1u << i;
                                    } else {
                                        ray->layerMask &= ~(1u << i);
                                    }
                                }
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponent");
            }
            if (ImGui::BeginPopup("AddComponent")) {
                if (!world.has<Raycast>(selected) && ImGui::MenuItem("Raycast")) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    world.add(selected, Raycast{});
                }
                if (!world.has<CharacterController>(selected) &&
                    ImGui::MenuItem("CharacterController")) {
                    addCharacterController(world, selected);
                }
                if (!world.has<FPSCamera>(selected) && ImGui::MenuItem("FPSCamera")) {
                    addFPSCamera(world, selected);
                }
                if (world.has<Transform>(selected)) {
                    if (!world.has<DirectionalLight>(selected) &&
                        ImGui::MenuItem("DirectionalLight")) {
                        world.add(selected, DirectionalLight{});
                    }
                    if (!world.has<PointLight>(selected) && ImGui::MenuItem("PointLight")) {
                        world.add(selected, PointLight{});
                    }
                    if (!world.has<SpotLight>(selected) && ImGui::MenuItem("SpotLight")) {
                        world.add(selected, SpotLight{});
                    }
                }
                if (!world.has<AudioListener>(selected) && world.has<Transform>(selected) &&
                    ImGui::MenuItem("AudioListener")) {
                    world.add(selected, AudioListener{});
                }
                if (!world.has<Skybox>(selected) && ImGui::MenuItem("Skybox")) {
                    world.add(selected, Skybox{});
                }
                if (!world.has<AudioSource>(selected) && ImGui::MenuItem("AudioSource")) {
                    world.add(selected, AudioSource{});
                }
                if (!world.has<Transform>(selected) && ImGui::MenuItem("Transform")) {
                    world.add(selected, Transform{});
                }
                if (!world.has<MeshRenderer>(selected) && ImGui::MenuItem("MeshRenderer")) {
                    world.add(selected, MeshRenderer{meshes[0]});
                }
                if (!world.has<Collider>(selected) && ImGui::MenuItem("Collider")) {
                    world.add(selected, Collider{});
                }
                if (!world.has<RigidBody>(selected) && ImGui::MenuItem("RigidBody")) {
                    world.add(selected, RigidBody{});
                }
                if (!world.has<Camera>(selected) && world.has<Transform>(selected) &&
                    ImGui::MenuItem("Camera")) {
                    world.add(selected, Camera{});
                }
                if (ImGui::MenuItem("LegacyScript")) {
                    if (auto* scripts = world.tryGet<LuaScript>(selected)) {
                        if (scripts->additional.size() < 63) {
                            scripts->additional.push_back({});
                        }
                    } else {
                        world.add(selected, LuaScript{});
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    ImGui::End();
}
