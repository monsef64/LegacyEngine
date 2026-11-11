#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

struct LevelObject {
    Entity entity;
    std::string name;
    bool hasCollider;
};

inline std::vector<LevelObject> levelObjects(World& world)
{
    std::vector<LevelObject> objects;
    world.each<Transform, MeshRenderer>([&](Entity e, Transform&, MeshRenderer&) {
        const auto* name=world.tryGet<ObjectName>(e);
        objects.push_back({e,name ? name->value : "Object " + std::to_string(e.index),world.has<Collider>(e)});
    });
    std::sort(objects.begin(),objects.end(),[](const LevelObject& a,const LevelObject& b) {
        return a.entity.index<b.entity.index;
    });
    return objects;
}

// Returns true when selection changes. The caller can end any active gizmo gesture.
inline bool drawLevelObjects(World& world, Entity& selected, bool selectionEnabled)
{
    bool changed=false;
    if (!world.alive(selected) && selected!=Entity{}) { selected={}; changed=true; }
    ImGui::SetNextWindowPos(ImVec2(16,360),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250,240),ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level Objects")) {
        const auto objects=levelObjects(world);
        ImGui::Text("Objects: %d",static_cast<int>(objects.size()));
        if (!selectionEnabled) ImGui::TextDisabled("Disable Spawn Shapes Mode to select");
        ImGui::BeginDisabled(!selectionEnabled);
        if (ImGui::Button("Clear selection")) { selected={}; changed=true; }
        ImGui::Separator();
        ImGui::BeginChild("Objects",ImVec2(0,0));
        for (const auto& object:objects) {
            ImGui::PushID(static_cast<int>(object.entity.index));
            ImGui::PushID(static_cast<int>(object.entity.generation));
            if (ImGui::Selectable(object.name.c_str(),selected==object.entity)) {
                changed=selected!=object.entity;
                selected=object.entity;
            }
            if (ImGui::IsItemHovered() && !object.hasCollider)
                ImGui::SetTooltip("Visual helper: no collider or transform gizmo");
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }
    ImGui::End();
    return changed;
}
