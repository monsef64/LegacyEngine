#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include "hierarchy.hpp"
#include "editor_layout.hpp"
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
    world.each<Transform>([&](Entity e, Transform&) {
        const auto* name=world.tryGet<ObjectName>(e);
        objects.push_back({e,name ? name->value : "Object " + std::to_string(e.index),world.has<Collider>(e)});
    });
    std::sort(objects.begin(),objects.end(),[](const LevelObject& a,const LevelObject& b) {
        return a.entity.index<b.entity.index;
    });
    return objects;
}

// Copy values before adding components: pool growth can invalidate references.
inline Entity duplicateLevelObject(World& world, Entity source) {
    if (!world.alive(source)) return {};
    const Entity copy = world.create();
    auto component = [&](auto value) {
        using T = decltype(value);
        if (auto* original = world.tryGet<T>(source)) { T data = *original; world.add(copy, data); }
    };
    component(Transform{}); component(MeshRenderer{}); component(Collider{});
    component(RigidBody{}); component(Parent{}); component(ObjectName{});
    if (auto* name = world.tryGet<ObjectName>(copy)) name->value += " (Copy)";
    return copy;
}
inline void deleteLevelObject(World& world, Entity object) {
    // Detach direct children, preserving their world-space poses.
    world.each<Parent>([&](Entity child, Parent& link) {
        if (link.entity == object) world.remove<Parent>(child);
    });
    world.destroy(object);
}

// Returns true when selection changes. The caller can end any active gizmo gesture.
inline bool drawLevelObjects(World& world, Entity& selected, bool selectionEnabled,const EditorRect* panel=nullptr)
{
    bool changed=false;
    if (!world.alive(selected) && selected!=Entity{}) { selected={}; changed=true; }
    if(panel)placeEditorPanel(*panel);
    else {ImGui::SetNextWindowPos(ImVec2(16,360),ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize(ImVec2(250,240),ImGuiCond_FirstUseEver);}
    if (ImGui::Begin(panel?"Hierarchy":"Level Objects",nullptr,panel?fixedPanelFlags:0)) {
        const auto objects=levelObjects(world);
        ImGui::Text("Objects: %d",static_cast<int>(objects.size()));
        if (!selectionEnabled) ImGui::TextDisabled("Disable Spawn Shapes Mode to select");
        ImGui::BeginDisabled(!selectionEnabled);
        if (ImGui::Button("Clear selection")) { selected={}; changed=true; }
        if (!panel && world.alive(selected)) {
            const Entity parent=getParent(world,selected);
            const auto* label=world.tryGet<ObjectName>(parent);
            if(ImGui::BeginCombo("Parent",parent==Entity{}?"None":label?label->value.c_str():"Object")) {
                if(ImGui::Selectable("None",parent==Entity{}))setParent(world,selected,{});
                for(const auto& object:objects) {
                    if(!canParent(world,selected,object.entity))continue;
                    ImGui::PushID(static_cast<int>(object.entity.index));
                    if(ImGui::Selectable(object.name.c_str(),parent==object.entity))setParent(world,selected,object.entity);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("Objects",ImVec2(0,0));
        auto row=[&](auto&& self,const LevelObject& object,int depth)->void {
            if (!world.alive(object.entity)) return;
            ImGui::PushID(static_cast<int>(object.entity.index));
            ImGui::PushID(static_cast<int>(object.entity.generation));
            if(depth)ImGui::Indent(static_cast<float>(depth)*12);
            if (ImGui::Selectable(object.name.c_str(),selected==object.entity)) {
                changed=selected!=object.entity;
                selected=object.entity;
            }
            if (ImGui::BeginPopupContextItem("Object actions")) {
                if (selected != object.entity) { selected = object.entity; changed = true; }
                ImGui::TextUnformatted(object.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate")) {
                    selected = duplicateLevelObject(world, object.entity); changed = true;
                }
                if (ImGui::MenuItem("Unparent", nullptr, false, getParent(world,object.entity)!=Entity{})) {
                    setParent(world, object.entity, {}); changed = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    deleteLevelObject(world, object.entity); selected = {}; changed = true;
                }
                ImGui::EndPopup();
            }
            if(ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("LEVEL_OBJECT",&object.entity,sizeof(Entity));
                ImGui::TextUnformatted(object.name.c_str());ImGui::EndDragDropSource();
            }
            if(ImGui::BeginDragDropTarget()) {
                if(const auto* payload=ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                    const Entity child=*static_cast<const Entity*>(payload->Data);
                    setParent(world,child,object.entity);
                }
                ImGui::EndDragDropTarget();
            }
            if(depth)ImGui::Unindent(static_cast<float>(depth)*12);
            ImGui::PopID();ImGui::PopID();
            if(depth<128)for(const auto& child:objects)
                if(getParent(world,child.entity)==object.entity)self(self,child,depth+1);
        };
        for(const auto& object:objects)if(getParent(world,object.entity)==Entity{})row(row,object,0);
        ImGui::EndChild();
        ImGui::EndDisabled();
    }
    ImGui::End();
    return changed;
}
