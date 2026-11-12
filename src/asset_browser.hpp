#pragma once
#include "assets.hpp"
#include "editor_layout.hpp"
#include "script_editor.hpp"
#include "project_dialog.hpp"

inline void fileIcon(const std::string& path) {
    auto ext=AssetManager::extension(path);
    const char* tag="FILE";ImU32 color=IM_COL32(150,165,185,255);
    if(ext==".lua"){tag="LUA";color=IM_COL32(100,165,255,255);}
    else if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".webp" || ext==".bmp"){tag="IMG";color=IM_COL32(110,205,160,255);}
    else if(ext==".obj" || ext==".fbx" || ext==".gltf" || ext==".glb"){tag="3D";color=IM_COL32(215,165,100,255);}
    else if(ext==".wav" || ext==".mp3" || ext==".ogg"){tag="SND";color=IM_COL32(200,130,225,255);}
    else if(ext==".legacy"){tag="PROJ";color=IM_COL32(105,200,225,255);}
    else if(ext==".txt" || ext==".json" || ext==".md"){tag="TXT";}
    auto pos=ImGui::GetCursorScreenPos();ImGui::Dummy(ImVec2(44,28));
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos,ImVec2(pos.x+40,pos.y+26),IM_COL32(35,45,60,255),4);
    draw->AddRect(pos,ImVec2(pos.x+40,pos.y+26),color,4);
    draw->AddText(ImVec2(pos.x+3,pos.y+4),color,tag);
    ImGui::SameLine();
}
class AssetBrowser {
public:
    AssetManager assets;
    std::string selected,openedScript;
    ProjectDialog dialog;
    void importFile(const std::string& path) {
        try{selected=assets.import(path);assets.error.clear();}catch(const std::exception& e){assets.error=e.what();}
    }
    void setRoot(const std::filesystem::path& root) {assets.root=root;selected.clear();openedScript.clear();}
    void draw(SDL_Window* window,World& world,Entity selectedObject,ScriptEditor& scripts,bool editable) {
        if(auto result=dialog.poll()) {if(!result->error.empty())assets.error=result->error;else if(!result->path.empty())importFile(result->path);}
        if(dockedEditor && !panelOpen[5])return;
        if(beginEditorPanel("Assets")) {
            ImGui::TextDisabled("%s",assets.root.string().c_str());
            ImGui::BeginDisabled(!editable || dialog.pending());
            if(ImGui::Button("Import..."))dialog.showImport(window);
            ImGui::SameLine();
            if(ImGui::Button("New Lua")) {
                try {
                    int n=1;std::string name="NewScript.lua";
                    while(std::filesystem::exists(assets.resolve(name)))name="NewScript"+std::to_string(++n)+".lua";
                    assets.writeScript(name,"-- self is the attached GameObject\nfunction onUpdate(dt)\n    -- self.getComponent(Transform).position.x = 1\nend\n");selected=name;
                }catch(const std::exception& e){assets.error=e.what();}
            }
            if(!openedScript.empty()) {
                ImGui::TextWrapped("Editing asset: %s",openedScript.c_str());
                if(ImGui::Button("Save Lua asset"))try{assets.writeScript(openedScript,scripts.source());}catch(const std::exception& e){assets.error=e.what();}
            }
            ImGui::TextWrapped("Drop files here to import. Drag Lua assets onto a GameObject or Inspector to attach.");
            if(!assets.error.empty())ImGui::TextWrapped("%s",assets.error.c_str());
            try {
                for(const auto& file:assets.files()) {
                    ImGui::PushID(file.c_str());fileIcon(file);
                    if(ImGui::Selectable(file.c_str(),selected==file,ImGuiSelectableFlags_AllowDoubleClick,ImVec2(0,28))) {
                        selected=file;
                        if(AssetManager::script(file) && ImGui::IsMouseDoubleClicked(0)) {
                            try {scripts.setSource(assets.readScript(file));openedScript=file;panelOpen[3]=true;}
                            catch(const std::exception& e){assets.error=e.what();}
                        }
                    }
                    if(ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("ASSET_PATH",file.c_str(),file.size()+1);
                        fileIcon(file);ImGui::TextUnformatted(file.c_str());ImGui::EndDragDropSource();
                    }
                    if(ImGui::BeginPopupContextItem("Asset actions")) {
                        if(ImGui::MenuItem("Attach Lua to selected object",nullptr,false,world.alive(selectedObject) && AssetManager::script(file)))attachScriptAsset(world,selectedObject,file);
                        if(ImGui::MenuItem("Rename...")) {selected=file;std::snprintf(rename_,sizeof(rename_),"%s",std::filesystem::path(file).filename().string().c_str());renameRequested_=true;}
                        if(ImGui::MenuItem("Delete...")){selected=file;deleteRequested_=true;}
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if(renameRequested_){ImGui::OpenPopup("Rename asset");renameRequested_=false;}
                if(deleteRequested_){ImGui::OpenPopup("Delete asset");deleteRequested_=false;}
                if(ImGui::BeginPopupModal("Rename asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::InputText("Name",rename_,sizeof(rename_));
                    if(ImGui::Button("Rename")) {
                        try {
                        std::filesystem::path leaf(rename_);
                        if(leaf.empty() || leaf.has_parent_path() || leaf=="..")assets.error="Use a filename without directories";
                        else {
                            auto relative=(std::filesystem::path(selected).parent_path()/leaf).generic_string();
                            if(std::filesystem::exists(assets.resolve(relative)))assets.error="An asset with that name exists";
                            else {
                                std::filesystem::rename(assets.resolve(selected),assets.resolve(relative));
                                world.each<LuaScript>([&](Entity,LuaScript& s){if(s.path==selected)s.path=relative;});
                                if(openedScript==selected)openedScript=relative;
                                selected=relative;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        }catch(const std::exception& e){assets.error=e.what();}
                    }
                    ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
                if(ImGui::BeginPopupModal("Delete asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("Delete %s? Script references will become missing. File operations are not covered by scene undo.",selected.c_str());
                    if(ImGui::Button("Delete"))try{std::filesystem::remove(assets.resolve(selected));if(openedScript==selected)openedScript.clear();selected.clear();ImGui::CloseCurrentPopup();}catch(const std::exception& e){assets.error=e.what();}
                    ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
            } catch(const std::exception& e){assets.error=e.what();}
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
private:
    char rename_[256]{};bool renameRequested_=false,deleteRequested_=false;
};
