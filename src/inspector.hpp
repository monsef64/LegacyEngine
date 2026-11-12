#pragma once
#include "editor_layout.hpp"
#include "level_objects.hpp"
#include "transform_edit.hpp"
#include <ImGuizmo.h>
#include <cstdio>
#include <cmath>

inline bool editVector(const char* label,Vec3& value,float speed=0.05f,float minimum=-1000000,float maximum=1000000) {
    float v[]{value.x,value.y,value.z};
    if(!ImGui::DragFloat3(label,v,speed,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp))return false;
    for(float n:v)if(!std::isfinite(n))return false;
    value={v[0],v[1],v[2]};return true;
}
inline bool componentHeader(const char* name) {return ImGui::CollapsingHeader(name,ImGuiTreeNodeFlags_DefaultOpen);}
inline void drawInspector(World& world,Entity selected,const EditorRect& rect,const MeshHandle* meshes,bool editingGizmo) {
    if(dockedEditor && !panelOpen[1])return;
    placeEditorPanel(rect);
    if(beginEditorPanel("Inspector",fixedPanelFlags)) {
        if(!world.alive(selected))ImGui::TextDisabled("Select a GameObject in Hierarchy or Scene.");
        else {
            ImGui::PushID(static_cast<int>(selected.index));
            ImGui::BeginDisabled(editingGizmo);
            auto* objectName=world.tryGet<ObjectName>(selected);
            char name[257];std::snprintf(name,sizeof(name),"%s",objectName?objectName->value.c_str():"");
            if(ImGui::InputText("Name",name,sizeof(name)))world.add(selected,ObjectName{name});
            const Entity parent=getParent(world,selected);const auto* parentName=world.tryGet<ObjectName>(parent);
            if(ImGui::BeginCombo("Parent",parent==Entity{}?"None":parentName?parentName->value.c_str():"Object")) {
                if(ImGui::Selectable("None",parent==Entity{}))setParent(world,selected,{});
                for(const auto& entry:levelObjects(world))if(canParent(world,selected,entry.entity)) {
                    ImGui::PushID(static_cast<int>(entry.entity.index));
                    if(ImGui::Selectable(entry.name.c_str(),parent==entry.entity))setParent(world,selected,entry.entity);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if(auto* t=world.tryGet<Transform>(selected);t && componentHeader("Transform")) {
                editVector("Position",t->position);
                Mat4 model=transformMatrix(t->position,t->rotation,t->scale);
                float position[3],rotation[3],scale[3];
                ImGuizmo::DecomposeMatrixToComponents(model.m,position,rotation,scale);
                if(ImGui::DragFloat3("Rotation (deg)",rotation,0.5f,-36000,36000,"%.1f",ImGuiSliderFlags_AlwaysClamp)) {
                    ImGuizmo::RecomposeMatrixFromComponents(position,rotation,scale,model.m);
                    applyGizmoMatrix(*t,model,TransformTool::Rotate,false);
                }
                const auto* c=world.tryGet<Collider>(selected);
                if(c && c->shape!=ColliderShape::Box) {
                    float size=t->scale.x;
                    if(ImGui::DragFloat("Uniform scale",&size,0.02f,0.05f,100,"%.3f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(size))t->scale={size,size,size};
                } else editVector("Scale",t->scale,0.02f,0.05f,100);
            }
            if(auto* r=world.tryGet<MeshRenderer>(selected);r && componentHeader("MeshRenderer")) {
                const char* names[]{"Cube","Sphere","Capsule","Cone"};int current=-1;
                for(int i=0;i<4;++i)if(r->mesh.id==meshes[i].id)current=i;
                if(ImGui::BeginCombo("Mesh",current<0?"Custom":names[current])) {
                    for(int i=0;i<4;++i)if(ImGui::Selectable(names[i],current==i)) {
                        r->mesh=meshes[i];
                        if(i!=0)if(auto* t=world.tryGet<Transform>(selected)) {
                            const float scale=std::clamp(std::max({std::abs(t->scale.x),std::abs(t->scale.y),std::abs(t->scale.z)}),0.05f,100.0f);
                            t->scale={scale,scale,scale};
                        }
                        if(auto* c=world.tryGet<Collider>(selected)){c->shape=static_cast<ColliderShape>(i);c->size={1,1,1};c->radius=0.5f;c->height=1;}
                    }
                    ImGui::EndCombo();
                }
                float color[]{r->color.x,r->color.y,r->color.z};
                if(ImGui::ColorEdit3("Color",color))r->color={color[0],color[1],color[2]};
                ImGui::Checkbox("Checkerboard",&r->checkerboard);
                ImGui::SliderFloat("Specular",&r->specular,0,1);
                if(ImGui::Button("Remove MeshRenderer"))world.remove<MeshRenderer>(selected);
            }
            if(auto* c=world.tryGet<Collider>(selected);c && componentHeader("Collider")) {
                const char* names[]{"Box","Sphere","Capsule","Cone"};int shape=static_cast<int>(c->shape);
                if(ImGui::Combo("Collider shape",&shape,names,4))c->shape=static_cast<ColliderShape>(shape);
                editVector("Center",c->center);
                if(c->shape==ColliderShape::Box)editVector("Size",c->size,0.02f,0.01f,10000);
                else {
                    ImGui::DragFloat("Radius",&c->radius,0.02f,0.01f,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                    if(c->shape!=ColliderShape::Sphere)ImGui::DragFloat("Height",&c->height,0.02f,0.01f,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                }
                ImGui::SliderFloat("Friction",&c->friction,0,1);ImGui::SliderFloat("Bounciness",&c->bounciness,0,1);
                if(ImGui::Button("Remove Collider"))world.remove<Collider>(selected);
            }
            if(auto* rb=world.tryGet<RigidBody>(selected);rb && componentHeader("RigidBody")) {
                ImGui::DragFloat("Mass",&rb->mass,0.1f,0,1000000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("Use gravity",&rb->useGravity);
                editVector("Linear velocity",rb->linearVelocity);editVector("Angular velocity",rb->angularVelocity);
                if(ImGui::Button("Remove RigidBody"))world.remove<RigidBody>(selected);
            }
            if(auto* script=world.tryGet<LuaScript>(selected);script && componentHeader("Lua Script")) {
                ImGui::Checkbox("Script enabled",&script->enabled);
                char path[4097];std::snprintf(path,sizeof(path),"%s",script->path.c_str());
                if(ImGui::InputText("Asset path",path,sizeof(path)))script->path=path;
                ImGui::TextDisabled("Drop a .lua asset below to assign it.");
                if(ImGui::Button("Remove Lua Script"))world.remove<LuaScript>(selected);
            }
            ImGui::Button("Drop Lua script here",ImVec2(-1,32));
            if(ImGui::BeginDragDropTarget()) {
                if(const auto* asset=ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    attachScriptAsset(world,selected,static_cast<const char*>(asset->Data));
                ImGui::EndDragDropTarget();
            }
            if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComponent");
            if(ImGui::BeginPopup("AddComponent")) {
                if(!world.has<Transform>(selected) && ImGui::MenuItem("Transform"))world.add(selected,Transform{});
                if(!world.has<MeshRenderer>(selected) && ImGui::MenuItem("MeshRenderer"))world.add(selected,MeshRenderer{meshes[0]});
                if(!world.has<Collider>(selected) && ImGui::MenuItem("Collider"))world.add(selected,Collider{});
                if(!world.has<RigidBody>(selected) && ImGui::MenuItem("RigidBody"))world.add(selected,RigidBody{});
                if(!world.has<LuaScript>(selected) && ImGui::MenuItem("Lua Script"))world.add(selected,LuaScript{});
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();ImGui::PopID();
        }
    }
    ImGui::End();
}
