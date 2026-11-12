#pragma once
#include "editor_layout.hpp"
#include "level_objects.hpp"
#include "scene_camera.hpp"
#include "rotation_editor.hpp"
#include "transform_edit.hpp"
#include <ImGuizmo.h>
#include <cstdio>
#include <cmath>

inline bool editVector(const char* label,Vec3& value,float speed=0.05f,float minimum=-1000000,float maximum=1000000) {
    ImGui::PushID(label);ImGui::TextUnformatted(label);
    float v[]{value.x,value.y,value.z};bool changed=false;
    const char* axes[]{"X","Y","Z"};
    const float axisWidth=std::max(20.0f,(ImGui::GetContentRegionAvail().x-90)/3);
    for(int i=0;i<3;++i){if(i)ImGui::SameLine();ImGui::TextUnformatted(axes[i]);ImGui::SameLine();ImGui::PushID(i);
        ImGui::SetNextItemWidth(axisWidth);changed|=ImGui::DragFloat("##axis",&v[i],speed,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();}
    ImGui::PopID();if(!changed)return false;
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
                static RotationEditorState rotationState;
                rotationState.sync(selected,t->rotation);
                Vec3 rotation=rotationState.angles;
                if(editVector("Rotation (deg)",rotation,.5f,-360,360))t->rotation=rotationState.apply(rotation);
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
            if(auto* camera=world.tryGet<Camera>(selected);camera && componentHeader("Camera")) {
                Camera edited=*camera;
                bool active=camera->active;
                if(ImGui::Checkbox("Active camera",&active)) {
                    if(active)activateCamera(world,selected);else camera->active=false;
                }
                edited.active=camera->active;
                ImGui::Checkbox("Orthographic",&edited.orthographic);
                ImGui::DragFloat("Field of view (degrees)",&edited.fieldOfView,.1f,1,178);
                ImGui::DragFloat("Orthographic size",&edited.orthographicSize,.1f,.01f,1000000);
                ImGui::DragFloat("Near clip",&edited.nearClip,.01f,.001f,edited.farClip-.001f);
                ImGui::DragFloat("Far clip",&edited.farClip,1,edited.nearClip+.001f,1000000);
                float viewport[]{edited.viewportX,edited.viewportY,edited.viewportWidth,edited.viewportHeight};
                if(ImGui::DragFloat4("Viewport",viewport,.005f,0,1)) {
                    edited.viewportX=viewport[0];edited.viewportY=viewport[1];edited.viewportWidth=viewport[2];edited.viewportHeight=viewport[3];
                }
                if(validCamera(edited))*camera=edited;
                if(ImGui::Button("Clear camera target")){camera->tracking=false;camera->following=false;camera->targetEntity={};}
                if(ImGui::Button("Remove Camera"))world.remove<Camera>(selected);
            }
            if(auto* a=world.tryGet<AudioSource>(selected);a && componentHeader("AudioSource")) {
                char clip[4097];std::snprintf(clip,sizeof(clip),"%s",a->clip.c_str());
                if(ImGui::InputText("AudioClip (.wav)",clip,sizeof(clip)))a->clip=clip;
                if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("ASSET_PATH")){std::string path=static_cast<const char*>(payload->Data);if(AssetManager::extension(path)==".wav")a->clip=path;}ImGui::EndDragDropTarget();}
                ImGui::SliderFloat("Volume",&a->volume,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);ImGui::SliderFloat("Pitch",&a->pitch,.1f,4,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Pan",&a->pan,-1,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);ImGui::DragFloat("Distance",&a->distance,.1f,0,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("Looping",&a->looping);ImGui::Checkbox("Mute",&a->muted);ImGui::Checkbox("Play on start",&a->playOnStart);
                if(ImGui::Button("Play audio"))a->play();
                ImGui::SameLine();if(ImGui::Button("Stop audio"))a->stop();
                if(ImGui::Button("Pause audio"))a->pause();
                ImGui::SameLine();if(ImGui::Button("Resume audio"))a->resume();
                if(ImGui::Button("Fade in (1s)"))a->fadeIn(1);
                ImGui::SameLine();if(ImGui::Button("Fade out (1s)"))a->fadeOut(1);
                ImGui::TextUnformatted(a->isPlaying()?"Playing":a->state==AudioSource::State::Paused?"Paused":"Stopped");
                if(ImGui::Button("Remove AudioSource"))world.remove<AudioSource>(selected);
            }
            if(auto* scripts=world.tryGet<LuaScript>(selected);scripts && componentHeader("LegacyScripts")) {
                int remove=-1;
                auto row=[&](std::string& path,bool& enabled,int index) {
                    ImGui::PushID(index);ImGui::Separator();
                    ImGui::Checkbox("Enabled",&enabled);
                    char buffer[4097];std::snprintf(buffer,sizeof(buffer),"%s",path.c_str());
                    if(ImGui::InputText("Asset path",buffer,sizeof(buffer)))path=buffer;
                    if(ImGui::Button("Remove script"))remove=index;
                    ImGui::PopID();
                };
                row(scripts->path,scripts->enabled,0);
                for(std::size_t i=0;i<scripts->additional.size();++i)row(scripts->additional[i].path,scripts->additional[i].enabled,static_cast<int>(i+1));
                if(remove==0) {
                    if(scripts->additional.empty())world.remove<LuaScript>(selected);
                    else {auto next=scripts->additional.front();scripts->path=next.path;scripts->enabled=next.enabled;scripts->additional.erase(scripts->additional.begin());}
                } else if(remove>0)scripts->additional.erase(scripts->additional.begin()+remove-1);
            }
            ImGui::Button("Drop LegacyScript (.ls) here",ImVec2(-1,32));
            if(ImGui::BeginDragDropTarget()) {
                if(const auto* asset=ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    attachScriptAsset(world,selected,static_cast<const char*>(asset->Data));
                ImGui::EndDragDropTarget();
            }
            if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComponent");
            if(ImGui::BeginPopup("AddComponent")) {
                if(!world.has<AudioSource>(selected) && ImGui::MenuItem("AudioSource"))world.add(selected,AudioSource{});
                if(!world.has<Transform>(selected) && ImGui::MenuItem("Transform"))world.add(selected,Transform{});
                if(!world.has<MeshRenderer>(selected) && ImGui::MenuItem("MeshRenderer"))world.add(selected,MeshRenderer{meshes[0]});
                if(!world.has<Collider>(selected) && ImGui::MenuItem("Collider"))world.add(selected,Collider{});
                if(!world.has<RigidBody>(selected) && ImGui::MenuItem("RigidBody"))world.add(selected,RigidBody{});
                if(!world.has<Camera>(selected) && world.has<Transform>(selected) && ImGui::MenuItem("Camera"))world.add(selected,Camera{});
                if(ImGui::MenuItem("LegacyScript")) {
                    if(auto* scripts=world.tryGet<LuaScript>(selected)){if(scripts->additional.size()<63)scripts->additional.push_back({});}
                    else world.add(selected,LuaScript{});
                }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();ImGui::PopID();
        }
    }
    ImGui::End();
}
