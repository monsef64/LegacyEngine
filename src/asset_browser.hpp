#pragma once
#include "assets.hpp"
#include "editor_layout.hpp"
#include "script_editor.hpp"
#include "project_dialog.hpp"
#include "editor_history.hpp"
#include "system_trash.hpp"
#include "project_assets.hpp"

inline void fileBadge(const std::string& path,ImVec2 pos) {
    auto ext=AssetManager::extension(path);
    const char* tag="FILE";ImU32 color=IM_COL32(150,165,185,255);
    if(ext==".ls" || ext==".lua"){tag="LS";color=IM_COL32(100,165,255,255);}
    else if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".webp" || ext==".bmp"){tag="IMG";color=IM_COL32(110,205,160,255);}
    else if(ext==".obj" || ext==".fbx" || ext==".gltf" || ext==".glb"){tag="3D";color=IM_COL32(215,165,100,255);}
    else if(ext==".wav" || ext==".mp3" || ext==".ogg"){tag="SND";color=IM_COL32(200,130,225,255);}
    else if(ext==".legacy"){tag="PROJ";color=IM_COL32(105,200,225,255);}
    else if(ext==".txt" || ext==".json" || ext==".md"){tag="TXT";}
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos,ImVec2(pos.x+40,pos.y+26),IM_COL32(35,45,60,255),4);
    draw->AddRect(pos,ImVec2(pos.x+40,pos.y+26),color,4);
    draw->AddText(ImVec2(pos.x+3,pos.y+4),color,tag);
}
inline void fileIcon(const std::string& path) {
    auto pos=ImGui::GetCursorScreenPos();ImGui::Dummy(ImVec2(44,28));fileBadge(path,pos);ImGui::SameLine();
}
class AssetBrowser {
    UntitledProjectAssets staging_;
public:
    AssetBrowser(){assets.root=staging_.path;}
    void setGridMode(bool grid){grid_=grid;}
    bool gridMode() const{return grid_;}
    AssetManager assets;
    std::string selected,openedScript;
    ProjectDialog dialog;
    void importFile(const std::string& path) {
        try{selected=assets.import(path);assets.error.clear();}catch(const std::exception& e){assets.error=e.what();}
    }
    void setRoot(const std::filesystem::path& root) {assets.root=root;selected.clear();openedScript.clear();}
    void draw(SDL_Window* window,World& world,Entity selectedObject,ScriptEditor& scripts,bool editable,EditorHistory* history=nullptr) {
        if(auto result=dialog.poll()) {if(!result->error.empty())assets.error=result->error;else if(!result->path.empty())importFile(result->path);}
        if(dockedEditor && !panelOpen[5])return;
        if(beginEditorPanel("Assets")) {
            const bool shortcutFocus=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId) && editable && !dialog.pending();
            ImGui::TextDisabled("%s",assets.root.string().c_str());
            if(ImGui::RadioButton("List",!grid_))grid_=false;
            ImGui::SameLine();if(ImGui::RadioButton("Grid",grid_))grid_=true;
            ImGui::BeginDisabled(!editable || dialog.pending());
            if(ImGui::Button("Import..."))dialog.showImport(window);
            ImGui::SameLine();
            if(ImGui::Button("New LegacyScript"))createFile(".ls");
            if(!openedScript.empty()) {
                ImGui::TextWrapped("Editing asset: %s",openedScript.c_str());
                if(ImGui::Button("Save script asset"))try{assets.writeScript(openedScript,scripts.source());}catch(const std::exception& e){assets.error=e.what();}
            }
            ImGui::TextWrapped("Drop files here to import. Drag LegacyScript assets onto a GameObject or Inspector to attach.");
            if(!assets.error.empty())ImGui::TextWrapped("%s",assets.error.c_str());
            try {
                const auto files=assets.files();
                const int columns=grid_?std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x/120),1,64):1;
                if(ImGui::BeginTable("Asset items",columns,ImGuiTableFlags_SizingStretchSame)) {
                for(const auto& file:files) {
                    ImGui::TableNextColumn();ImGui::PushID(file.c_str());
                    if(!grid_)fileIcon(file);
                    const bool clicked=ImGui::Selectable(grid_?"##tile":file.c_str(),selected==file,ImGuiSelectableFlags_AllowDoubleClick,ImVec2(0,grid_?84.0f:28.0f));
                    if(grid_) {
                        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(a,b,true);
                        fileBadge(file,ImVec2((a.x+b.x)*.5f-20,a.y+4));
                        draw->AddText(nullptr,0,ImVec2(a.x+4,a.y+34),ImGui::GetColorU32(ImGuiCol_Text),file.c_str(),nullptr,std::max(1.0f,b.x-a.x-8));
                        draw->PopClipRect();
                    }
                    if(clicked) {
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
                        if(ImGui::MenuItem("Attach LegacyScript to selected object",nullptr,false,world.alive(selectedObject) && AssetManager::script(file)))attachScriptAsset(world,selectedObject,file);
                        if(ImGui::MenuItem("Rename...","F2")) {selected=file;renameRequested_=true;}
                        if(ImGui::MenuItem("Move to Trash","Del")){selected=file;deleteRequested_=true;}
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
                }
                if(ImGui::BeginPopupContextWindow("Assets empty actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)) {
                    if(ImGui::BeginMenu("Create")) {
                        if(ImGui::MenuItem("LegacyScript (.ls)"))createFile(".ls");
                        if(ImGui::MenuItem("Text file (.txt)"))createFile(".txt");
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
                if(shortcutFocus && !selected.empty()) {
                    if(ImGui::IsKeyPressed(ImGuiKey_F2,false))renameRequested_=true;
                    if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))deleteRequested_=true;
                }
                if(renameRequested_){
                    std::snprintf(rename_,sizeof(rename_),"%s",std::filesystem::path(selected).filename().string().c_str());
                    ImGui::OpenPopup("Rename asset");renameRequested_=false;renameFocus_=true;
                }
                if(deleteRequested_){
                    deleteRequested_=false;
                    if(history && !selected.empty()) {
                        auto receipt=std::make_shared<TrashFile>(assets.resolve(selected));
                        if(history->perform(world,selectedObject,[receipt](bool redo){if(redo)receipt->remove();else receipt->restore();})) {
                            if(openedScript==selected)openedScript.clear();
                            selected.clear();
                        } else assets.error=history->error;
                    } else assets.error="Asset deletion requires editor history";
                }
                if(ImGui::BeginPopupModal("Rename asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    if(renameFocus_){ImGui::SetKeyboardFocusHere();renameFocus_=false;}
                    const bool renameEnter=ImGui::InputText("Name",rename_,sizeof(rename_),ImGuiInputTextFlags_EnterReturnsTrue);
                    if(ImGui::Button("Rename") || renameEnter) {
                        try {
                        std::filesystem::path leaf(rename_);
                        if(leaf.empty() || leaf.has_parent_path() || leaf=="..")assets.error="Use a filename without directories";
                        else {
                            auto relative=(std::filesystem::path(selected).parent_path()/leaf).generic_string();
                            if(std::filesystem::exists(assets.resolve(relative)))assets.error="An asset with that name exists";
                            else {
                                std::filesystem::rename(assets.resolve(selected),assets.resolve(relative));
                                world.each<LuaScript>([&](Entity,LuaScript& s){if(s.path==selected)s.path=relative;for(auto& entry:s.additional)if(entry.path==selected)entry.path=relative;});
                                if(openedScript==selected)openedScript=relative;
                                selected=relative;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        }catch(const std::exception& e){assets.error=e.what();}
                    }
                    ImGui::SameLine();if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
            } catch(const std::exception& e){assets.error=e.what();}
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
private:
    void createFile(const std::string& extension) {
        try {
            const std::string base=extension==".ls"?"NewScript":"NewText";
            std::string name=base+extension;int n=2;
            while(std::filesystem::exists(assets.resolve(name)))name=base+" ("+std::to_string(n++)+")"+extension;
            if(extension==".ls")assets.writeScript(name,"");
            else {std::filesystem::create_directories(assets.root);std::ofstream out(assets.resolve(name));if(!out)throw std::runtime_error("Cannot create text asset");}
            selected=name;
        }catch(const std::exception& e){assets.error=e.what();}
    }
    char rename_[256]{};bool renameRequested_=false,deleteRequested_=false,grid_=false,renameFocus_=false;
};
