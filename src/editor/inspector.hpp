#pragma once
#include "animation/animation_clip.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_clip.hpp"
#include "editor/asset_picker.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/component_clipboard.hpp"
#include "editor/editor_layout.hpp"
#include "editor/level_objects.hpp"
#include "editor/rotation_editor.hpp"
#include "editor/transform_edit.hpp"
#include "editor/value_context.hpp"
#include "effects/laser_io.hpp"
#include "effects/light_shafts_io.hpp"
#include "materials/material_io.hpp"
#include "models/model_asset.hpp"
#include "project/layers.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/script_exports.hpp"
#include <ImGuizmo.h>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

inline bool editVector(const char* label,
                       Vec3& value,
                       float speed = 0.05f,
                       float minimum = -1000000,
                       float maximum = 1000000) {
    ImGui::PushID(label);
    float v[]{value.x, value.y, value.z};
    bool changed = false;
    const char* axes[]{"X", "Y", "Z"};
    const bool narrow = ImGui::GetContentRegionAvail().x < 340;
    if (narrow) {
        ImGui::TextUnformatted(label);
        changed |= EditorValues::context(v, 3, minimum, maximum, "Vector values");
    }
    if (ImGui::BeginTable("##Vector",
                          narrow ? 2 : 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX)) {
        if (narrow) {
            ImGui::TableSetupColumn("Axis", ImGuiTableColumnFlags_WidthFixed, 18);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        } else {
            ImGui::TableSetupColumn("Label",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    std::min(100.f, ImGui::GetContentRegionAvail().x * .25f));
            for (int i = 0; i < 3; ++i) {
                ImGui::TableSetupColumn(axes[i], ImGuiTableColumnFlags_WidthStretch);
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            changed |= EditorValues::context(v, 3, minimum, maximum, "Vector values");
        }
        for (int i = 0; i < 3; ++i) {
            if (narrow) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
            } else {
                ImGui::TableSetColumnIndex(i + 1);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(axes[i]);
            if (narrow) {
                ImGui::TableSetColumnIndex(1);
            } else {
                ImGui::SameLine();
            }
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(std::max(12.f, ImGui::GetContentRegionAvail().x));
            changed |= EditorValues::DragFloat(
                "##axis", &v[i], speed, minimum, maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
        }
        ImGui::EndTable();
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
inline void drawParticleSettings(ParticleSystem& p, const AssetManager* assets) {
    if (ImGui::BeginCombo("Preset", "Select preset...")) {
        for (int i = 0; i < particlePresetCount; ++i) {
            if (ImGui::Selectable(particlePresetNames[i])) {
                applyParticlePreset(p, i);
            }
        }
        ImGui::EndCombo();
    }
    Json edited = Particles::settings(p);
    bool changed = false;
    auto fields = [&](const char* group, std::initializer_list<const char*> names) {
        if (!ImGui::TreeNodeEx(group, ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        for (const auto* name : names) {
            ImGui::PushID(name);
            std::string label;
            for (const char* c = name; *c; ++c) {
                if (c != name && std::isupper(static_cast<unsigned char>(*c))) {
                    label += ' ';
                }
                label += *c;
            }
            if (!label.empty()) {
                label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
            }
            const char* caption = label.c_str();
            auto& value = edited[name];
            if (value.is_boolean()) {
                bool b = value.get<bool>();
                if (EditorValues::Checkbox(caption, &b)) {
                    value = b;
                    changed = true;
                }
            } else if (value.is_array()) {
                Vec3 v{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                const bool color = std::strstr(name, "Color") || !std::strcmp(name, "color");
                if (color ? EditorValues::ColorEdit3(caption, &v.x) : editVector(caption, v)) {
                    value = Json::array({v.x, v.y, v.z});
                    changed = true;
                }
            } else if (value.is_number_integer()) {
                int v = value.get<int>();
                bool edit = false;
                if (!std::strcmp(name, "shape")) {
                    edit = EditorValues::Combo(caption, &v, "Point\0Box\0Sphere\0Cone\0");
                } else if (!std::strcmp(name, "blend")) {
                    edit = EditorValues::Combo(caption, &v, "Alpha\0Additive\0");
                } else if (!std::strcmp(name, "sprite")) {
                    edit = EditorValues::Combo(caption, &v, "Fire\0Smoke\0Sparkles\0");
                } else {
                    edit = EditorValues::InputInt(caption, &v);
                }
                if (edit) {
                    value = v;
                    changed = true;
                }
            } else if (value.is_number()) {
                float f = value.get<float>();
                if (EditorValues::DragFloat(caption, &f, .02f)) {
                    value = f;
                    changed = true;
                }
            }
        }
        ImGui::TreePop();
    };
    fields("Emission",
           {"playOnAwake",
            "loop",
            "duration",
            "startDelay",
            "rate",
            "burstCount",
            "maxParticles",
            "seed"});
    fields("Shape and motion",
           {"shape",
            "radius",
            "boxSize",
            "coneAngle",
            "direction",
            "localSpace",
            "speedMin",
            "speedMax",
            "velocity",
            "gravity",
            "drag"});
    fields("Physics", {"collision", "restitution", "collisionFriction"});
    fields("Appearance",
           {"lifetimeMin",
            "lifetimeMax",
            "sizeMin",
            "sizeMax",
            "endSize",
            "color",
            "endColor",
            "alpha",
            "endAlpha",
            "rotationMin",
            "rotationMax",
            "angularVelocity",
            "blend",
            "sprite"});
    fields("Texture animation", {"useSpriteSheet"});
    ImGui::BeginDisabled(!edited["useSpriteSheet"].get<bool>());
    fields("Sprite sheet", {"sheetColumns", "sheetRows", "frameRate"});
    ImGui::EndDisabled();
    std::string texture = p.texture;
    if (assetPicker("Texture override", texture, assets, [](const std::string& path) {
            return materialTexturePath(path);
        })) {
        edited["texture"] = texture;
        changed = true;
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* drop = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const std::string path = static_cast<const char*>(drop->Data);
            if (materialTexturePath(path)) {
                edited["texture"] = path;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    static std::string error;
    if (changed) {
        try {
            const auto settings = Particles::parse(edited);
            const bool restart = settings.localSpace != p.localSpace || settings.seed != p.seed;
            static_cast<ParticleSettings&>(p) = settings;
            if (restart) {
                p.restart();
            }
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    if (!error.empty()) {
        ImGui::TextWrapped("%s", error.c_str());
    }
}

template <class T>
inline bool
componentHeader(World& world, Entity entity, const char* name, const T& defaults = T{}) {
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(.16f, .23f, .31f, 1));
    const char* display = !std::strcmp(name, "CloudSystem")     ? "Clouds"
                          : !std::strcmp(name, "CameraEffects") ? "Camera Effects"
                          : !std::strcmp(name, "FPSCamera")     ? "FPS"
                                                                : name;
    const std::string title = std::string(display) + "###" + name;
    const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
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
        if (ImGui::MenuItem("Reset")) {
            if constexpr (std::is_same_v<T, Transform>) {
                setLocalTransform(world, entity, Transform{});
            } else {
                if constexpr (std::is_base_of_v<LightSettings, T>) {
                    detachLightBake(world, world.get<T>(entity));
                }
                world.get<T>(entity) = defaults;
            }
        }
        ImGui::Separator();
        bool allowed = true;
        if constexpr (std::is_same_v<T, Transform>) {
            allowed = !world.has<Camera>(entity) && !world.has<AudioListener>(entity) &&
                      !world.has<DirectionalLight>(entity) && !world.has<PointLight>(entity) &&
                      !world.has<SpotLight>(entity) && !world.has<Laser>(entity) &&
                      !world.has<Vehicle>(entity) && !world.has<CloudSystem>(entity) &&
                      !world.has<River>(entity) && !world.has<Fog>(entity) &&
                      !world.has<CameraEffects>(entity) && !world.has<Terrain>(entity) &&
                      !world.has<LightShafts>(entity) && !world.has<CharacterController>(entity) &&
                      !world.has<FPSCamera>(entity);
        }
        if constexpr (std::is_same_v<T, Collider> || std::is_same_v<T, RigidBody>) {
            allowed = !world.has<CharacterController>(entity) && !world.has<Vehicle>(entity);
        }
        if constexpr (std::is_same_v<T, Camera>) {
            allowed = !world.has<FPSCamera>(entity);
        }
        remove = ImGui::MenuItem("Remove component", nullptr, false, allowed);
        if (!allowed) {
            ImGui::TextDisabled(
                "Remove dependent Vehicle / Camera / listener / lights / controller / FPSCamera first");
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
        if constexpr (std::is_same_v<T, Animation>) {
            type = 16;
        }
        if constexpr (std::is_same_v<T, ParticleSystem>) {
            type = 17;
        }
        if constexpr (std::is_same_v<T, Laser>) {
            type = 18;
        }
        if constexpr (std::is_same_v<T, CloudSystem>) {
            type = 23;
        }
        if constexpr (std::is_same_v<T, Vehicle>) {
            type = 25;
        }
        if constexpr (std::is_same_v<T, River>) {
            type = 24;
        }
        if constexpr (std::is_same_v<T, Fog>) {
            type = 22;
        }
        if constexpr (std::is_same_v<T, CameraEffects>) {
            type = 21;
        }
        if constexpr (std::is_same_v<T, Terrain>) {
            type = 20;
        }
        if constexpr (std::is_same_v<T, LightShafts>) {
            type = 19;
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
            const char* names[]{"Transform",     "RigidBody",
                                "Collider",      "MeshRenderer",
                                "ObjectName",    "LegacyScripts",
                                "Camera",        "AudioSource",
                                "AudioListener", "DirectionalLight",
                                "PointLight",    "SpotLight",
                                "Skybox",        "CharacterController",
                                "FPSCamera",     "Raycast",
                                "Animation",     "ParticleSystem",
                                "Laser",         "LightShafts",
                                "Terrain",       "CameraEffects",
                                "Fog",           "CloudSystem",
                                "River",         "Vehicle"};
            for (int i = 0; i < 26; ++i) {
                if (!std::strcmp(name, names[i])) {
                    target = i;
                }
            }
            if (source.entity == entity && target >= 0 && source.type != target) {
                auto& order = world.has<InspectorOrder>(entity) ? world.get<InspectorOrder>(entity)
                                                                : world.add(entity,
                                                                            InspectorOrder{{0,
                                                                                            3,
                                                                                            2,
                                                                                            1,
                                                                                            6,
                                                                                            9,
                                                                                            10,
                                                                                            11,
                                                                                            12,
                                                                                            8,
                                                                                            7,
                                                                                            5,
                                                                                            13,
                                                                                            14,
                                                                                            15,
                                                                                            16,
                                                                                            17,
                                                                                            18,
                                                                                            19}});
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
        if constexpr (std::is_base_of_v<LightSettings, T>) {
            detachLightBake(world, world.get<T>(entity));
        }
        world.remove<T>(entity);
        return false;
    }
    return open;
}
inline void drawScriptExports(World& world,
                              std::vector<ScriptField>& fields,
                              const AssetManager* assets = nullptr) {
    const char* componentTypes[]{"Transform",     "RigidBody",
                                 "Collider",      "MeshRenderer",
                                 "ObjectName",    "LuaScript",
                                 "Camera",        "AudioSource",
                                 "AudioListener", "DirectionalLight",
                                 "PointLight",    "SpotLight",
                                 "Skybox",        "CharacterController",
                                 "FPSCamera",     "Raycast",
                                 "Animation",     "ParticleSystem",
                                 "Laser",         "LightShafts",
                                 "Terrain",       "CameraEffects",
                                 "Fog",           "CloudSystem",
                                 "River",         "Vehicle"};
    for (auto& field : fields) {
        ImGui::PushID(field.name.c_str());
        ImGui::Text("%s (%s)", field.name.c_str(), field.type.c_str());
        if (field.type == "number") {
            EditorValues::DragFloat("##value",
                                    &field.number,
                                    .1f,
                                    -1000000,
                                    1000000,
                                    "%.3f",
                                    ImGuiSliderFlags_AlwaysClamp);
        } else if (field.type == "boolean") {
            EditorValues::Checkbox("##value", &field.boolean);
        } else if (field.type == "string") {
            char value[4097];
            std::snprintf(value, sizeof(value), "%s", field.text.c_str());
            if (EditorValues::InputText("##value", value, sizeof(value))) {
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
                        if (target.type >= 0 && target.type < 26 &&
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
        changed |= EditorValues::Combo(
            "Shader",
            &edited.shader,
            "Diffuse\0Diffuse Bumped\0Diffuse Transparent\0Diffuse Reflection\0");
        changed |= EditorValues::ColorEdit3("Color", &edited.color.x);
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
            changed |= EditorValues::SliderFloat("Normal strength", &edited.normalStrength, 0, 4);
        }
        float tiling[]{edited.tilingX, edited.tilingY};
        if (EditorValues::DragFloat2("UV tiling", tiling, .05f, -1000, 1000)) {
            edited.tilingX = tiling[0];
            edited.tilingY = tiling[1];
            changed = true;
        }
        changed |= EditorValues::SliderFloat("Specular", &edited.specular, 0, 1);
        ImGui::TextWrapped(
            "Shared asset: edits are saved immediately and affect all objects using this material.");
        if (changed && validMaterial(edited)) {
            saveMaterial(assets, path, edited);
        }
    } catch (const std::exception& error) {
        ImGui::TextWrapped("%s", error.what());
    }
}

inline void
drawModelImportInspector(World& world, const AssetManager& assets, const std::string& path) {
    try {
        static std::string current, settings, error, warnings;
        static int partCount = 0;
        static float scale = 1;
        static unsigned revision = ~0u;
        static std::filesystem::file_time_type modified{};
        static auto checked = std::chrono::steady_clock::time_point{};
        const auto key = assets.root.generic_string() + "/" + path;
        auto now = std::chrono::steady_clock::now();
        bool refresh = current != key || revision != modelAssetRevision;
        if (refresh || now - checked > std::chrono::milliseconds(500)) {
            checked = now;
            const auto stamp = std::filesystem::last_write_time(assets.resolve(path));
            refresh |= stamp != modified;
            modified = stamp;
        }
        if (refresh) {
            auto document = modelDocument(assets, path);
            settings = document.value("settings", path);
            if (settings != path) {
                document = modelDocument(assets, settings);
            }
            scale = modelImportScale(document);
            partCount = document.contains("parts") ? int(document.at("parts").size()) : 0;
            warnings = document.value("warnings", std::string{});
            current = key;
            revision = modelAssetRevision;
            error.clear();
        }
        ImGui::TextWrapped("Model import settings: %s", settings.c_str());
        drawAssetPreview(assets, path, std::min(384.f, ImGui::GetContentRegionAvail().x), 1);
        EditorValues::InputFloat("Import scale", &scale, .001f, .1f, "%.6f");
        ImGui::TextWrapped(
            "Applied before Transform scale (0.000001 to 1000000). Shared instances and their Box collider bounds are updated.");
        if (ImGui::Button("Apply import scale")) {
            try {
                saveModelImportScale(assets, settings, scale);
                world.each<MeshRenderer, Collider>(
                    [&](Entity, MeshRenderer& renderer, Collider& collider) {
                        if (renderer.model.empty() || collider.shape != ColliderShape::Box) {
                            return;
                        }
                        const auto doc = modelDocument(assets, renderer.model);
                        if (renderer.model != settings &&
                            doc.value("settings", std::string{}) != settings) {
                            return;
                        }
                        const auto mesh = loadModelAsset(assets, renderer.model);
                        Vec3 low = mesh.vertices.front().position, high = low;
                        for (const auto& v : mesh.vertices) {
                            low = {std::min(low.x, v.position.x),
                                   std::min(low.y, v.position.y),
                                   std::min(low.z, v.position.z)};
                            high = {std::max(high.x, v.position.x),
                                    std::max(high.y, v.position.y),
                                    std::max(high.z, v.position.z)};
                        }
                        collider.center = (low + high) * .5f;
                        auto size = high - low;
                        collider.size = {std::max(.001f, size.x),
                                         std::max(.001f, size.y),
                                         std::max(.001f, size.z)};
                    });
                error.clear();
            } catch (const std::exception& e) {
                error = e.what();
            }
        }
        if (partCount) {
            ImGui::Text("Mesh parts: %d", partCount);
        }
        if (!warnings.empty()) {
            ImGui::TextWrapped("%s", warnings.c_str());
        }
        ImGui::TextWrapped(
            "Skeletal clips can be previewed by clicking the model in Assets. Materials live in its data folder.");
        if (!error.empty()) {
            ImGui::TextWrapped("%s", error.c_str());
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
                          const std::string& selectedAsset = {},
                          bool runtime = false) {
    if (dockedEditor && !panelOpen[1]) {
        return;
    }
    placeEditorPanel(rect);
    if (beginEditorPanel("Inspector", fixedPanelFlags)) {
        if (assets && modelAsset(selectedAsset)) {
            ImGui::BeginDisabled(editingGizmo || runtime);
            drawModelImportInspector(world, *assets, selectedAsset);
            ImGui::EndDisabled();
        } else if (assets && materialAsset(selectedAsset)) {
            ImGui::BeginDisabled(editingGizmo || runtime);
            drawMaterialAssetInspector(*assets, selectedAsset);
            ImGui::EndDisabled();
        } else if (!world.alive(selected)) {
            ImGui::TextDisabled("Select a GameObject in Hierarchy or Scene.");
        } else {
            ImGui::PushID(static_cast<int>(selected.index));
            ImGui::BeginDisabled(editingGizmo);
            const float available = ImGui::GetContentRegionAvail().x;
            bool objectActive =
                !world.has<ObjectActivity>(selected) || world.get<ObjectActivity>(selected).enabled;
            if (EditorValues::Checkbox("##Active", &objectActive)) {
                setObjectActive(world, selected, objectActive);
            }
            ImGui::SameLine();
            auto* objectName = world.tryGet<ObjectName>(selected);
            char name[257];
            std::snprintf(name, sizeof(name), "%s", objectName ? objectName->value.c_str() : "");
            ImGui::SetNextItemWidth(std::max(30.f, available - ImGui::GetFrameHeight() - 100.f));
            if (EditorValues::InputText("##Name", name, sizeof(name))) {
                world.add(selected, ObjectName{name});
            }
            ImGui::SameLine();
            if (auto* t = world.tryGet<Transform>(selected)) {
                EditorValues::Checkbox("Static", &t->isStatic);
            }
            int layer = LayerSettings::effective(world, selected);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##Layer", ("Layer   " + LayerSettings::names[layer]).c_str())) {
                for (int i = 0; i < 32; ++i) {
                    if (!LayerSettings::names[i].empty() &&
                        ImGui::Selectable(LayerSettings::names[i].c_str(), layer == i)) {
                        world.add(selected, ObjectLayer{i});
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Separator();
            auto ordered = world.has<InspectorOrder>(selected)
                               ? world.get<InspectorOrder>(selected).types
                               : std::vector<int>{0,  3,  2,  1,  6,  9,  10, 11, 12, 8,  7,  5, 13,
                                                  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
            for (int id : {0,  3,  2,  1,  6,  9,  10, 11, 12, 8,  7,  5, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}) {
                if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
                    ordered.push_back(id);
                }
            }
            for (int componentID : ordered) {
                if (auto* p = world.tryGet<ParticleSystem>(selected);
                    componentID == 17 && p &&
                    componentHeader<ParticleSystem>(world, selected, "ParticleSystem")) {
                    drawParticleSettings(*p, assets);
                }

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
                    if (editVector("Rotation", rotation, .5f, -360, 360)) {
                        edited.rotation = rotationState.apply(rotation);
                        transformChanged = true;
                    }
                    const auto* c = world.tryGet<Collider>(selected);
                    if (c && c->shape != ColliderShape::Box) {
                        float size = edited.scale.x;
                        if (EditorValues::DragFloat("Uniform scale",
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
                    componentHeader<MeshRenderer>(
                        world, selected, "MeshRenderer", MeshRenderer{meshes[0]})) {
                    if (assetPicker("Model asset",
                                    r->model,
                                    assets,
                                    [](const std::string& path) { return modelAsset(path); }) &&
                        r->model.empty()) {
                        r->mesh = meshes[0];
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const auto* drop = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                            std::string path = static_cast<const char*>(drop->Data);
                            if (modelAsset(path)) {
                                r->model = path;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
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
                                r->model.clear();
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
                            static char materialName[128] = "NewMaterial";
                            static bool focusMaterialName = false;
                            bool createRequested = false;
                            if (ImGui::BeginPopupContextItem("Material slot actions")) {
                                createRequested =
                                    ImGui::MenuItem("Create Material", nullptr, false, !runtime);
                                ImGui::EndPopup();
                            }
                            if (!runtime && r->material.empty() &&
                                ImGui::SmallButton("Create Material")) {
                                createRequested = true;
                            }
                            if (createRequested) {
                                std::snprintf(materialName, sizeof(materialName), "NewMaterial");
                                materialError.clear();
                                focusMaterialName = true;
                                ImGui::OpenPopup("Create Material asset");
                            }
                            if (!runtime &&
                                ImGui::BeginPopupModal("Create Material asset",
                                                       nullptr,
                                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                                ImGui::TextUnformatted("Name the material (saved in Assets)");
                                if (focusMaterialName) {
                                    ImGui::SetKeyboardFocusHere();
                                    focusMaterialName = false;
                                }
                                const bool enter =
                                    EditorValues::InputText("Name",
                                                            materialName,
                                                            sizeof(materialName),
                                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                                                ImGuiInputTextFlags_AutoSelectAll);
                                if (ImGui::Button("Create") || enter) {
                                    try {
                                        std::string name = materialName;
                                        const auto first = name.find_first_not_of(" \t");
                                        name = first == std::string::npos
                                                   ? ""
                                                   : name.substr(first,
                                                                 name.find_last_not_of(" \t") -
                                                                     first + 1);
                                        if (AssetManager::extension(name) == ".mat") {
                                            name.resize(name.size() - 4);
                                        }
                                        if (name.empty() || name == "." || name == ".." ||
                                            name.back() == '.' || name.back() == ' ' ||
                                            name.find_first_of("/\\:*?\"<>|") !=
                                                std::string::npos) {
                                            throw std::runtime_error(
                                                "Enter a filename without folders or special characters");
                                        }
                                        for (unsigned char c : name) {
                                            if (c < 32) {
                                                throw std::runtime_error(
                                                    "Invalid filename character");
                                            }
                                        }
                                        std::string device =
                                            std::filesystem::path(name).stem().string();
                                        for (auto& c : device) {
                                            c = static_cast<char>(
                                                std::toupper(static_cast<unsigned char>(c)));
                                        }
                                        if (device == "CON" || device == "PRN" || device == "AUX" ||
                                            device == "NUL" ||
                                            (device.size() == 4 &&
                                             (device.substr(0, 3) == "COM" ||
                                              device.substr(0, 3) == "LPT") &&
                                             device[3] >= '1' && device[3] <= '9')) {
                                            throw std::runtime_error(
                                                "That filename is reserved on Windows");
                                        }
                                        const auto path = name + ".mat";
                                        if (std::filesystem::exists(assets->resolve(path))) {
                                            throw std::runtime_error(
                                                "A material with that name already exists");
                                        }
                                        MaterialProperties material;
                                        material.color = r->color;
                                        material.specular = r->specular;
                                        saveMaterial(*assets, path, material);
                                        r->material = path;
                                        r->materialOverrides = 0;
                                        materialError.clear();
                                        ImGui::CloseCurrentPopup();
                                    } catch (const std::exception& error) {
                                        materialError = error.what();
                                    }
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Cancel") ||
                                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                                    materialError.clear();
                                    ImGui::CloseCurrentPopup();
                                }
                                if (!materialError.empty()) {
                                    ImGui::TextWrapped("%s", materialError.c_str());
                                }
                                ImGui::EndPopup();
                            }
                            if (!r->material.empty()) {
                                auto edited = loadMaterial(*assets, r->material);
                                if (runtime) {
                                    edited = applyMaterialOverrides(
                                        edited, r->materialValues, r->materialOverrides);
                                }
                                if (ImGui::TreeNodeEx("Material properties",
                                                      ImGuiTreeNodeFlags_DefaultOpen)) {
                                    bool changed = false;
                                    changed |= EditorValues::Combo(
                                        "Shader",
                                        &edited.shader,
                                        "Diffuse\0Diffuse Bumped\0Diffuse Transparent\0Diffuse Reflection\0");
                                    changed |=
                                        EditorValues::ColorEdit3("Material color", &edited.color.x);
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
                                        changed |= EditorValues::SliderFloat(
                                            "Normal strength", &edited.normalStrength, 0, 4);
                                    }
                                    float tiling[]{edited.tilingX, edited.tilingY};
                                    if (EditorValues::DragFloat2(
                                            "UV tiling", tiling, .05f, -1000, 1000)) {
                                        edited.tilingX = tiling[0];
                                        edited.tilingY = tiling[1];
                                        changed = true;
                                    }
                                    changed |= EditorValues::SliderFloat(
                                        "Material specular", &edited.specular, 0, 1);
                                    ImGui::TextDisabled(
                                        runtime
                                            ? "Temporary instance overrides (discarded on Stop)."
                                            : "Shared asset: edits affect all meshes using this material.");
                                    if (changed && validMaterial(edited)) {
                                        try {
                                            if (runtime) {
                                                r->materialValues = edited;
                                                r->materialOverrides = 127;
                                            } else {
                                                saveMaterial(*assets, r->material, edited);
                                            }
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
                        if (EditorValues::ColorEdit3("Color", color)) {
                            r->color = {color[0], color[1], color[2]};
                        }
                        EditorValues::Checkbox("Checkerboard", &r->checkerboard);
                        EditorValues::SliderFloat("Specular", &r->specular, 0, 1);
                    }
                }
                if (auto* c = world.tryGet<Collider>(selected);
                    componentID == 2 && c &&
                    componentHeader<Collider>(world, selected, "Collider")) {
                    const char* names[]{"Box", "Sphere", "Capsule", "Cone", "Mesh"};
                    int shape = static_cast<int>(c->shape);
                    if (EditorValues::Combo("Collider shape", &shape, names, 5)) {
                        c->shape = static_cast<ColliderShape>(shape);
                        if (c->shape == ColliderShape::Mesh) {
                            c->center = {};
                            c->meshDetail = 15;
                        }
                    }
                    editVector("Center", c->center);
                    if (c->shape == ColliderShape::Mesh) {
                        assetPicker("Mesh source (None = rendered model)",
                                    c->model,
                                    assets,
                                    [](const std::string& path) { return modelAsset(path); });
                        if (ImGui::BeginDragDropTarget()) {
                            if (const auto* drop = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                std::string path = static_cast<const char*>(drop->Data);
                                if (modelAsset(path)) {
                                    c->model = path;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        EditorValues::SliderInt("Mesh detail", &c->meshDetail, 2, 15);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip(
                                "Lower = fewer triangles. 15 preserves source triangles. Simplification can remove small features.");
                        }
                        ImGui::TextWrapped(
                            "Static triangle-mesh collider. None uses this object's rendered .mesh model. An explicit source overrides it. No cube fallback. RigidBody movement is disabled for this shape.");
                        const auto* visual = world.tryGet<MeshRenderer>(selected);
                        if (c->model.empty() && (!visual || visual->model.empty())) {
                            ImGui::TextWrapped(
                                "No model source: choose a .mesh asset above, or add this collider to a model's mesh child. Primitive meshes require an explicit model source.");
                        }
                    } else if (c->shape == ColliderShape::Box) {
                        editVector("Size", c->size, 0.02f, 0.01f, 10000);
                    } else {
                        EditorValues::DragFloat("Radius",
                                                &c->radius,
                                                0.02f,
                                                0.01f,
                                                10000,
                                                "%.3f",
                                                ImGuiSliderFlags_AlwaysClamp);
                        if (c->shape != ColliderShape::Sphere) {
                            EditorValues::DragFloat("Height",
                                                    &c->height,
                                                    0.02f,
                                                    0.01f,
                                                    10000,
                                                    "%.3f",
                                                    ImGuiSliderFlags_AlwaysClamp);
                        }
                    }
                    EditorValues::Checkbox("Is trigger", &c->isTrigger);
                    ImGui::SeparatorText("Contact surface");
                    ImGui::TextDisabled(
                        "Surface properties also apply to static colliders (no RigidBody).");
                    EditorValues::SliderFloat("Friction", &c->friction, 0, 1);
                    EditorValues::SliderFloat("Bounciness", &c->bounciness, 0, 1);
                }
                if (auto* rb = world.tryGet<RigidBody>(selected);
                    componentID == 1 && rb &&
                    componentHeader<RigidBody>(world, selected, "RigidBody")) {
                    EditorValues::DragFloat(
                        "Mass", &rb->mass, 0.1f, 0, 1000000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::Checkbox("Use gravity", &rb->useGravity);
                    EditorValues::Checkbox("Float on rivers", &rb->floatOnRivers);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Opt-in buoyancy and water damping. Requires a dynamic body, gravity and a non-trigger primitive collider. Only active during simulation.");
                    }
                    EditorValues::Checkbox("Kinematic", &rb->isKinematic);
                    EditorValues::DragFloat(
                        "Drag", &rb->drag, .01f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::DragFloat("Angular drag",
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
                    if (EditorValues::Checkbox("Active camera", &active)) {
                        if (active) {
                            activateCamera(world, selected);
                        } else {
                            camera->active = false;
                        }
                    }
                    edited.active = camera->active;
                    EditorValues::Checkbox("Orthographic", &edited.orthographic);
                    EditorValues::DragFloat(
                        "Field of view (degrees)", &edited.fieldOfView, .1f, 1, 178);
                    EditorValues::DragFloat(
                        "Orthographic size", &edited.orthographicSize, .1f, .01f, 1000000);
                    EditorValues::DragFloat(
                        "Near clip", &edited.nearClip, .001f, .020f, edited.farClip - .001f);
                    EditorValues::DragFloat(
                        "Far clip", &edited.farClip, 1, edited.nearClip + .001f, 1000000);
                    float viewport[]{edited.viewportX,
                                     edited.viewportY,
                                     edited.viewportWidth,
                                     edited.viewportHeight};
                    if (EditorValues::DragFloat4("Viewport", viewport, .005f, 0, 1)) {
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
                if (componentID == 23) {
                    if (auto* component = world.tryGet<CloudSystem>(selected);
                        component && componentHeader<CloudSystem>(world, selected, "CloudSystem")) {
                        auto edited = *component;
                        auto* c = &edited;
                        EditorValues::Checkbox("Enabled", &c->enabled);
                        EditorValues::Checkbox("Volumetric", &c->volumetric);
                        EditorValues::ColorEdit3("Color", &c->color.x);
                        editVector("Size", c->size, .1f, .1f, 10000);
                        EditorValues::SliderFloat("Coverage", &c->coverage, 0, 1);
                        EditorValues::SliderFloat("Density", &c->density, 0, 10);
                        EditorValues::DragFloat("Noise scale", &c->scale, .001f, .001f, 10);
                        EditorValues::DragFloat("Wind speed", &c->speed, .1f, -100, 100);
                        EditorValues::SliderFloat("Opacity", &c->opacity, 0, 1);
                        EditorValues::SliderInt("Volume steps", &c->steps, 4, 24);
                        ImGui::TextWrapped(
                            "2D: one procedural layer. Volumetric: bounded box raymarch. Move the Transform to cloud altitude; keep volumes away from intersecting opaque geometry.");
                        if (Environment::valid(edited)) {
                            *component = edited;
                        }
                    }
                }
                if (componentID == 25) {
                    if (auto* v = world.tryGet<Vehicle>(selected);
                        v && componentHeader<Vehicle>(world, selected, "Vehicle")) {
                        auto edited = *v;
                        EditorValues::Checkbox("Enabled", &edited.enabled);
#define VEHICLE_UI(name, value, lo, hi)                                                            \
    EditorValues::DragFloat(                                                                       \
        #name, &edited.name, .05f, lo, hi, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                        VEHICLE_SETTINGS(VEHICLE_UI)
#undef VEHICLE_UI
                        if (Vehicles::valid(edited)) {
                            *v = edited;
                        }
                        ImGui::Text("Speed: %.1f km/h | Grounded wheels: %d/4",
                                    v->speed * 3.6f,
                                    v->groundedWheels);
                        ImGui::TextWrapped(
                            "Local -Z is forward. Four-wheel raycast suspension, front steering, all-wheel drive, rear handbrake. Requires a dynamic RigidBody and a primitive chassis Collider. Drive through Lua throttle/steering/brake/handbrake properties.");
                    }
                }
                if (componentID == 24) {
                    if (auto* component = world.tryGet<River>(selected);
                        component && componentHeader<River>(world, selected, "River")) {
                        auto edited = *component;
                        auto* r = &edited;
                        EditorValues::Checkbox("Enabled", &r->enabled);
                        EditorValues::Checkbox("Edit curve in Scene", &r->editCurve);
                        EditorValues::ColorEdit3("Water color", &r->color.x);
                        EditorValues::DragFloat("Width", &r->width, .1f, .05f, 1000);
                        EditorValues::SliderFloat("Opacity", &r->opacity, 0, 1);
                        EditorValues::Checkbox("Flowing", &r->flowing);
                        EditorValues::SliderFloat("Flow amount", &r->flowAmount, 0, 2);
                        EditorValues::DragFloat("Flow speed", &r->speed, .1f, -100, 100);
                        EditorValues::Checkbox("Transparent water shader", &r->transparent);
                        EditorValues::Checkbox("Collider contact effects", &r->collisionEffects);
                        EditorValues::SliderFloat("Contact strength", &r->collisionStrength, 0, 2);
                        EditorValues::DragFloat("Ripple scale", &r->waveScale, .05f, .01f, 100);
                        EditorValues::SliderFloat("Surface detail", &r->waveStrength, 0, 1);
                        EditorValues::SliderFloat("Edge foam", &r->foam, 0, 1);
                        EditorValues::SliderInt("Curve subdivisions", &r->subdivisions, 2, 32);
                        if (r->points.size() < 64 && ImGui::Button("Add curve point")) {
                            auto n = r->points.size();
                            r->points.push_back(r->points.back() +
                                                (r->points[n - 1] - r->points[n - 2]));
                        }
                        if (r->points.size() > 2 && ImGui::Button("Remove last point")) {
                            r->points.pop_back();
                        }
                        for (int i = 0; i < int(r->points.size()); ++i) {
                            ImGui::PushID(i);
                            editVector(("Point " + std::to_string(i + 1)).c_str(),
                                       r->points[i],
                                       .1f,
                                       -100000,
                                       100000);
                            ImGui::PopID();
                        }
                        ImGui::TextWrapped(
                            "Scene: enable curve editing there or here. Drag points on XZ; Ctrl-drag changes height. Shift-click places a point. Points are local to Transform. No automatic terrain carving, collision or physical fluid simulation.");
                        if (Environment::valid(edited)) {
                            *component = edited;
                        }
                    }
                }
                if (componentID == 22) {
                    if (auto* f = world.tryGet<Fog>(selected);
                        f && componentHeader<Fog>(world, selected, "Fog")) {
                        auto edited = *f;
                        EditorValues::Checkbox("Enabled", &edited.enabled);
                        EditorValues::ColorEdit3("Color", &edited.color.x);
                        EditorValues::DragFloat("Density", &edited.density, .001f, 0, 10);
                        EditorValues::DragFloat(
                            "Start distance", &edited.start, .1f, 0, edited.end - .001f);
                        EditorValues::DragFloat(
                            "End distance", &edited.end, 1, edited.start + .001f, 1000000);
                        if (Fogs::valid(edited)) {
                            *f = edited;
                        }
                        ImGui::TextWrapped(
                            "Global distance fog. First enabled active Fog in entity order is used.");
                    }
                }
                if (componentID == 21) {
                    if (auto* fx = world.tryGet<CameraEffects>(selected);
                        fx && componentHeader<CameraEffects>(world, selected, "CameraEffects")) {
                        if (!world.has<Camera>(selected)) {
                            ImGui::TextWrapped("Add Camera to this object to use effects.");
                        }
                        EditorValues::Checkbox("Enabled", &fx->enabled);
                        const int first[]{0, 2, 6, 7, 10, 11, 12, 13, 13, 15, 17};
                        const int last[]{2, 6, 7, 10, 11, 12, 13, 13, 15, 17, 19};
                        for (int i = 1; i < int(CameraFx::boolFields.size()); ++i) {
                            const auto& effect = CameraFx::boolFields[i];
                            if (ImGui::TreeNode(effect.name,
                                                "%s%s",
                                                effect.label,
                                                fx->*effect.member ? " (on)" : "")) {
                                EditorValues::Checkbox("Enabled", &(fx->*effect.member));
                                for (int k = first[i - 1]; k < last[i - 1]; ++k) {
                                    const auto& f = CameraFx::floatFields[k];
                                    if (k == 15) {
                                        int mode = fx->nightVisionMode >= .5f ? 1 : 0;
                                        if (EditorValues::Combo(
                                                "Mode", &mode, "Green\0Infrared\0")) {
                                            fx->nightVisionMode = float(mode);
                                        }
                                    } else {
                                        EditorValues::SliderFloat(
                                            f.label, &(fx->*f.member), f.low, f.high, "%.3f");
                                    }
                                }
                                if (i == 8) {
                                    ImGui::TextWrapped(
                                        "Single-frame FXAA anti-aliasing; no extra controls.");
                                }
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TextWrapped(
                            "Fast OpenGL screen-space effects. Motion blur is camera-only. AO/DOF use opaque depth; transparent surfaces do not write it. AA is FXAA-style, not TAA/MSAA.");
                    }
                }
                if (componentID == 20) {
                    if (auto* terrain = world.tryGet<Terrain>(selected);
                        terrain && componentHeader<Terrain>(world, selected, "Terrain")) {
                        ImGui::TextWrapped("%s",
                                           terrain->asset.empty() ? "Unsaved terrain"
                                                                  : terrain->asset.c_str());
                        if (ImGui::Button("Open terrain tools")) {
                            Terrains::openTools = true;
                        }
                    }
                }
                if (componentID == 19) {
                    if (auto* shaft = world.tryGet<LightShafts>(selected);
                        shaft && componentHeader<LightShafts>(world, selected, "LightShafts")) {
                        auto edited = *shaft;
                        EditorValues::Checkbox("Enabled", &edited.enabled);
                        editVector("Local direction", edited.direction);
                        EditorValues::ColorEdit3("Shaft color", &edited.color.x);
                        EditorValues::DragFloat("Height", &edited.height, .1f, .01f, 1000);
                        EditorValues::DragFloat("Start width", &edited.width, .01f, .001f, 100);
                        EditorValues::DragFloat("End width", &edited.endWidth, .05f, .001f, 500);
                        EditorValues::DragFloat("Shaft intensity", &edited.intensity, .02f, 0, 100);
                        EditorValues::SliderFloat("Density", &edited.density, 0, 1);
                        EditorValues::SliderFloat("Edge falloff", &edited.falloff, .1f, 8);
                        EditorValues::SliderFloat("Streaks", &edited.streaks, 0, 64);
                        EditorValues::SliderFloat("Start fade", &edited.fade, .01f, 1);
                        EditorValues::DragFloat(
                            "Camera near fade", &edited.nearFade, .05f, .01f, 100);
                        EditorValues::Checkbox("Use baked profile texture",
                                               &edited.useBakedTexture);
                        assetPicker("Profile texture",
                                    edited.texture,
                                    assets,
                                    [](const std::string& p) { return materialTexturePath(p); });
                        static std::string shaftError;
                        ImGui::BeginDisabled(runtime);
                        if (assets && ImGui::Button("Bake profile to PNG")) {
                            try {
                                edited.texture = Shafts::bake(*assets, selected, edited);
                                edited.useBakedTexture = true;
                                shaftError.clear();
                            } catch (const std::exception& e) {
                                shaftError = e.what();
                            }
                        }
                        ImGui::EndDisabled();
                        if (!shaftError.empty()) {
                            ImGui::TextWrapped("%s", shaftError.c_str());
                        }
                        ImGui::TextWrapped(
                            "Fast depth-tested visual shaft, not raymarched lighting. Profile baking caches edge/streak/fade shading only, not scene occlusion. Rebake after changing profile controls.");
                        try {
                            *shaft = Shafts::parse(Shafts::settings(edited));
                        } catch (const std::exception&) {
                        }
                    }
                }
                if (componentID == 18) {
                    if (auto* laser = world.tryGet<Laser>(selected);
                        laser && componentHeader<Laser>(world, selected, "Laser")) {
                        LaserSettings edited = *laser;
                        EditorValues::Checkbox("Enabled", &edited.enabled);
                        EditorValues::ColorEdit3("Beam color", &edited.color.x);
                        EditorValues::DragFloat("Beam intensity", &edited.intensity, .05f, 0, 100);
                        EditorValues::DragFloat("Beam range", &edited.range, .1f, .01f, 10000);
                        EditorValues::DragFloat(
                            "Core width", &edited.width, .001f, .001f, 10, "%.3f");
                        edited.glowWidth = std::max(edited.width, edited.glowWidth);
                        EditorValues::DragFloat(
                            "Glow width", &edited.glowWidth, .005f, edited.width, 100, "%.3f");
                        EditorValues::DragFloat(
                            "Impact size", &edited.impactSize, .005f, .001f, 100, "%.3f");
                        try {
                            Lasers::parse(Lasers::settings(edited));
                            static_cast<LaserSettings&>(*laser) = edited;
                        } catch (const std::exception&) {
                        }
                        ImGui::TextWrapped(
                            "Local -Z beam stops at the first active collider (excluding itself). Additive glow is depth-tested; no damage or forces are applied.");
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
                    if (EditorValues::Combo("Mode", &edited.mode, "Dynamic\0Static\0")) {
                        detachLightBake(world, *light);
                        edited.bake.clear();
                    }
                    ImGui::TextDisabled(edited.mode == 0 ? "Realtime light"
                                        : edited.bake.empty()
                                            ? "Not baked: realtime fallback"
                                            : "Baked snapshot (rebake after edits)");
                    EditorValues::Checkbox("Enabled", &edited.enabled);
                    EditorValues::ColorEdit3("Color", &edited.color.x);
                    EditorValues::DragFloat("Intensity", &edited.intensity, .05f, 0, 1000);
                    if (kind != 0) {
                        EditorValues::DragFloat("Range", &edited.range, .1f, .1f, 10000);
                    }
                    if (kind == 2) {
                        assetPicker("Cookie", edited.cookie, assets, [](const std::string& path) {
                            return validLightCookie(path);
                        });
                        if (ImGui::BeginDragDropTarget()) {
                            if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                const std::string path = static_cast<const char*>(payload->Data);
                                if (validLightCookie(path)) {
                                    edited.cookie = path;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (!edited.cookie.empty()) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear cookie")) {
                                edited.cookie.clear();
                            }
                        }
                        ImGui::TextWrapped(
                            "Cookie RGB times alpha modulates the cone. Local X/Y orient the projection; rotate the light to roll the image.");
                        EditorValues::DragFloat(
                            "Inner angle", &edited.innerAngle, .1f, 0, edited.outerAngle - .1f);
                        EditorValues::DragFloat(
                            "Outer angle", &edited.outerAngle, .1f, edited.innerAngle + .1f, 85);
                    }
                    EditorValues::DragFloat(kind == 0 ? "Sun angular softness" : "Source radius",
                                            &edited.softness,
                                            .01f,
                                            0,
                                            10,
                                            "%.3f");
                    EditorValues::Checkbox("Cast dynamic shadows", &edited.shadows);
                    EditorValues::Combo("Shadow quality",
                                        &edited.shadowQuality,
                                        "Off\0Low (256)\0Medium (512)\0High (1024)\0");
                    if (kind == 0) {
                        EditorValues::DragFloat(
                            "Shadow coverage", &edited.shadowDistance, .5f, 1, 1000);
                    }
                    EditorValues::DragFloat(
                        "Shadow bias", &edited.shadowBias, .0001f, 0, .05f, "%.4f");
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
                    EditorValues::Checkbox("Skybox enabled", &sky->enabled);
                    EditorValues::Combo("Skybox type", &sky->type, "Cubemap asset\0HDR panorama\0");
                    EditorValues::ColorEdit3("Skybox color", &sky->color.x);
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
                    EditorValues::Checkbox("Listener enabled", &listener->enabled);
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
                    EditorValues::SliderFloat(
                        "Volume", &a->volume, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::SliderFloat(
                        "Pitch", &a->pitch, .1f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::SliderFloat(
                        "Pan", &a->pan, -1, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::DragFloat("Max Distance",
                                            &a->distance,
                                            .1f,
                                            0,
                                            10000,
                                            "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::SliderFloat(
                        "Spatial Blend", &a->blend, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::Checkbox("Looping", &a->looping);
                    EditorValues::Checkbox("Mute", &a->muted);
                    EditorValues::Checkbox("Play on start", &a->playOnStart);
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
                        EditorValues::Checkbox("Enabled", &enabled);
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
                    EditorValues::DragFloat(
                        "Move speed", &c->speed, .1f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::DragFloat("Sprint multiplier",
                                            &c->sprintMultiplier,
                                            .05f,
                                            1,
                                            10,
                                            "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::DragFloat("Jump speed",
                                            &c->jumpSpeed,
                                            .1f,
                                            0,
                                            100,
                                            "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::Checkbox("Can move", &c->canMove);
                    ImGui::SameLine();
                    EditorValues::Checkbox("Can jump", &c->canJump);
                    ImGui::SameLine();
                    EditorValues::Checkbox("Can sprint", &c->canSprint);
                }
                if (auto* f = world.tryGet<FPSCamera>(selected);
                    componentID == 14 && f &&
                    componentHeader<FPSCamera>(world, selected, "FPSCamera")) {
                    EditorValues::Checkbox("Freecam", &f->isFreecam);
                    EditorValues::Checkbox("Mouse look", &f->mouseLook);
                    EditorValues::DragFloat("Look sensitivity",
                                            &f->sensitivity,
                                            .01f,
                                            .01f,
                                            5,
                                            "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
                    EditorValues::DragFloat("Freecam speed",
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
                    EditorValues::Combo("Query type", &ray->type, "Line\0LineOfSight\0Sphere\0");
                    editVector("Origin offset", ray->offset);
                    EditorValues::DragFloat("Distance",
                                            &ray->distance,
                                            .1f,
                                            .01f,
                                            10000,
                                            "%.2f",
                                            ImGuiSliderFlags_AlwaysClamp);
                    if (ray->type == 2) {
                        EditorValues::DragFloat("Radius",
                                                &ray->radius,
                                                .1f,
                                                .01f,
                                                10000,
                                                "%.2f",
                                                ImGuiSliderFlags_AlwaysClamp);
                    }
                    if (ray->type == 1) {
                        EditorValues::SliderFloat("Fan angle", &ray->angle, 1, 360);
                        EditorValues::SliderInt("Fan samples", &ray->samples, 2, 181);
                    }
                    EditorValues::Checkbox("Include triggers", &ray->includeTriggers);
                    if (ImGui::TreeNode("Query layers")) {
                        for (int i = 0; i < 32; ++i) {
                            if (!LayerSettings::names[i].empty()) {
                                bool enabled = ray->layerMask & (1u << i);
                                if (EditorValues::Checkbox(LayerSettings::names[i].c_str(),
                                                           &enabled)) {
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
            if (auto* a = world.tryGet<Animation>(selected);
                a && componentHeader<Animation>(world, selected, "Animation")) {
                assetPicker("Animation clip", a->clip, assets, [](const std::string& path) {
                    return animationAsset(path);
                });
                EditorValues::Checkbox("Loop", &a->loop);
                EditorValues::Checkbox("Play on start", &a->playOnStart);
                if (ImGui::Button("Play##Animation")) {
                    a->request(AnimationAction::Play);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop##Animation")) {
                    a->request(AnimationAction::Stop);
                }
                ImGui::SameLine();
                if (ImGui::Button("Pause##Animation")) {
                    a->request(AnimationAction::Pause);
                }
                ImGui::SameLine();
                if (ImGui::Button("Resume##Animation")) {
                    a->request(AnimationAction::Resume);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##Animation")) {
                    a->request(AnimationAction::Reset);
                }
                static std::string fadeTarget;
                static float fadeSeconds = .25f;
                assetPicker("Cross-fade target", fadeTarget, assets, [](const std::string& p) {
                    return animationAsset(p);
                });
                EditorValues::DragFloat("Cross-fade seconds",
                                        &fadeSeconds,
                                        .01f,
                                        0,
                                        3600,
                                        "%.2f",
                                        ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::Button("Cross-fade##Animation") && !fadeTarget.empty()) {
                    a->request(AnimationAction::CrossFade, fadeTarget, fadeSeconds);
                }
                ImGui::Text("%s - %.3f seconds",
                            a->isPlaying ? "Playing"
                            : a->paused  ? "Paused"
                                         : "Stopped",
                            a->time);
                if (!a->error.empty()) {
                    ImGui::TextWrapped("%s", a->error.c_str());
                }
                ImGui::TextDisabled(
                    "Author .leanim clips in Windows > Animation. Runtime controls require Play mode.");
            }
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponent");
            }
            if (ImGui::BeginPopup("AddComponent")) {
                if (!world.has<Vehicle>(selected) &&
                    ImGui::MenuItem(
                        "Vehicle", nullptr, false, !world.has<CharacterController>(selected))) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    if (!world.has<RigidBody>(selected)) {
                        RigidBody rb;
                        rb.mass = 1000;
                        world.add(selected, rb);
                    }
                    if (!world.has<Collider>(selected)) {
                        Collider c;
                        c.size = {1.8f, .6f, 4.f};
                        world.add(selected, c);
                    }
                    world.add(selected, Vehicle{});
                }
                if (ImGui::BeginMenu("Environment")) {
                    if (!world.has<CloudSystem>(selected) && ImGui::MenuItem("Clouds")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{{0, 20, 0}});
                        }
                        world.add(selected, CloudSystem{});
                    }
                    if (!world.has<Fog>(selected) && ImGui::MenuItem("Fog")) {
                        world.add(selected, Fog{});
                    }
                    if (!world.has<River>(selected) && ImGui::MenuItem("River")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, River{});
                    }
                    if (!world.has<Terrain>(selected) && ImGui::MenuItem("Terrain")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, Terrain{});
                        Terrains::openTools = true;
                    }
                    if (!world.has<Skybox>(selected) && ImGui::MenuItem("Skybox")) {
                        world.add(selected, Skybox{});
                    }
                    ImGui::EndMenu();
                }
                if (!world.has<CameraEffects>(selected) && ImGui::MenuItem("Camera Effects")) {
                    world.add(selected, CameraEffects{});
                }
                if (!world.has<LightShafts>(selected) && ImGui::MenuItem("LightShafts")) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    world.add(selected, LightShafts{});
                }
                if (!world.has<Laser>(selected) && ImGui::MenuItem("Laser")) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    world.add(selected, Laser{});
                }
                if (!world.has<ParticleSystem>(selected) && ImGui::MenuItem("Particle System")) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    world.add(selected, ParticleSystem{});
                }
                if (!world.has<Animation>(selected) && ImGui::MenuItem("Animation")) {
                    world.add(selected, Animation{});
                }
                if (!world.has<Raycast>(selected) && ImGui::MenuItem("Raycast")) {
                    if (!world.has<Transform>(selected)) {
                        world.add(selected, Transform{});
                    }
                    world.add(selected, Raycast{});
                }
                if (ImGui::BeginMenu("Physics")) {
                    if (!world.has<RigidBody>(selected) && ImGui::MenuItem("Rigidbody")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, RigidBody{});
                    }
                    if (!world.has<CharacterController>(selected) &&
                        ImGui::MenuItem("Character Controller",
                                        nullptr,
                                        false,
                                        !world.has<Vehicle>(selected))) {
                        addCharacterController(world, selected);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Cameras")) {
                    if (!world.has<Camera>(selected) && ImGui::MenuItem("Camera")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, Camera{});
                    }
                    if (!world.has<FPSCamera>(selected) && ImGui::MenuItem("FPS")) {
                        addFPSCamera(world, selected);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Lights")) {
                    if (!world.has<DirectionalLight>(selected) &&
                        ImGui::MenuItem("DirectionalLight")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, DirectionalLight{});
                    }
                    if (!world.has<PointLight>(selected) && ImGui::MenuItem("PointLight")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, PointLight{});
                    }
                    if (!world.has<SpotLight>(selected) && ImGui::MenuItem("SpotLight")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, SpotLight{});
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Audio")) {
                    if (!world.has<AudioSource>(selected) && ImGui::MenuItem("Audio Source")) {
                        world.add(selected, AudioSource{});
                    }
                    if (!world.has<AudioListener>(selected) && ImGui::MenuItem("Audio Listener")) {
                        if (!world.has<Transform>(selected)) {
                            world.add(selected, Transform{});
                        }
                        world.add(selected, AudioListener{});
                    }
                    ImGui::EndMenu();
                }
                if (!world.has<Transform>(selected) && ImGui::MenuItem("Transform")) {
                    world.add(selected, Transform{});
                }
                if (!world.has<MeshRenderer>(selected) && ImGui::MenuItem("MeshRenderer")) {
                    world.add(selected, MeshRenderer{meshes[0]});
                }
                if (ImGui::BeginMenu("Collider", !world.has<Collider>(selected))) {
                    const char* names[]{"Box", "Sphere", "Capsule", "Cone", "Mesh"};
                    for (int shape = 0; shape < 5; ++shape) {
                        if (ImGui::MenuItem(names[shape])) {
                            if (!world.has<Transform>(selected)) {
                                world.add(selected, Transform{});
                            }
                            Collider collider;
                            collider.shape = static_cast<ColliderShape>(shape);
                            world.add(selected, collider);
                        }
                    }
                    ImGui::EndMenu();
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
