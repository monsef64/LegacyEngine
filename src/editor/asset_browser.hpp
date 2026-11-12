#pragma once
#include "assets/assets.hpp"
#include "audio/audio_preview.hpp"
#include "editor/editor_settings.hpp"
#include "editor/editor_layout.hpp"
#include "editor/script_editor.hpp"
#include "platform/project_dialog.hpp"
#include "editor/editor_history.hpp"
#include "platform/system_trash.hpp"
#include "project/project_assets.hpp"

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
    std::string selected,openedScript,currentFolder;
    EditorSettings settings;
    std::filesystem::path projectFolder;
    ProjectDialog dialog;
    AudioPreview preview;
    void importFile(const std::string& path) {
        try{selected=assets.import(path);assets.error.clear();}catch(const std::exception& e){assets.error=e.what();}
    }
    void setRoot(const std::filesystem::path& root) {preview.stop();assets.root=root;currentFolder.clear();selected.clear();openedScript.clear();}
    void draw(SDL_Window* window,World& world,Entity selectedObject,ScriptEditor& scripts,bool editable,EditorHistory* history=nullptr) {
        if(auto result=dialog.poll()) {if(!result->error.empty())assets.error=result->error;else if(!result->path.empty())importFile(result->path);}
        preview.update();
        if(dockedEditor && !panelOpen[5])return;
        if(beginEditorPanel("Assets")) {
            const bool shortcutFocus=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId) && editable && !dialog.pending();
            ImGui::TextDisabled("%s",(assets.root/currentFolder).string().c_str());
            if(!currentFolder.empty() && ImGui::Button("Up one folder")){currentFolder=std::filesystem::path(currentFolder).parent_path().generic_string();selected.clear();}
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
            if(!preview.error.empty())ImGui::TextWrapped("%s",preview.error.c_str());
            ImGui::BeginChild("Asset files",ImVec2(0,-32));
            try {
                std::vector<std::string> files;
                const auto folder=assets.resolve(currentFolder);
                if(std::filesystem::is_directory(folder))for(const auto& entry:std::filesystem::directory_iterator(folder)){
                    if(files.size()>=10000)break;
                    if(!entry.is_symlink() && (entry.is_directory() || entry.is_regular_file())){auto path=entry.path().lexically_relative(assets.root).generic_string();assets.resolve(path);files.push_back(path);}
                }
                std::sort(files.begin(),files.end());
                const int columns=grid_?std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x/120),1,64):1;
                if(ImGui::BeginTable("Asset items",columns,ImGuiTableFlags_SizingStretchSame)) {
                for(const auto& file:files) {
                    const bool folderItem=std::filesystem::is_directory(assets.resolve(file));
                    const std::string display=std::filesystem::path(file).filename().string()+(folderItem?"/":"");
                    ImGui::TableNextColumn();ImGui::PushID(file.c_str());
                    if(!grid_)fileIcon(file);
                    const bool clicked=ImGui::Selectable(grid_?"##tile":display.c_str(),selected==file,ImGuiSelectableFlags_AllowDoubleClick|ImGuiSelectableFlags_AllowOverlap,ImVec2(0,grid_?84.0f:28.0f));
                    const auto itemMin=ImGui::GetItemRectMin(),itemMax=ImGui::GetItemRectMax();
                    const bool itemHovered=ImGui::IsMouseHoveringRect(itemMin,itemMax);
                    if(grid_) {
                        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(a,b,true);
                        fileBadge(file,ImVec2((a.x+b.x)*.5f-20,a.y+4));
                        draw->AddText(nullptr,0,ImVec2(a.x+4,a.y+34),ImGui::GetColorU32(ImGuiCol_Text),display.c_str(),nullptr,std::max(1.0f,b.x-a.x-8));
                        draw->PopClipRect();
                    }
                    if(clicked) {
                        selected=file;
                        if(folderItem && ImGui::IsMouseDoubleClicked(0)){currentFolder=file;selected.clear();}
                        if(!folderItem && AssetManager::script(file) && ImGui::IsMouseDoubleClicked(0)) {
                            try {if(settings.external){if(!settings.openExternal(assets.resolve(file),true))assets.error=settings.error;}else {scripts.setSource(assets.readScript(file));openedScript=file;panelOpen[3]=true;}}
                            catch(const std::exception& e){assets.error=e.what();}
                        }
                    }
                    if(!folderItem && ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("ASSET_PATH",file.c_str(),file.size()+1);
                        fileIcon(file);ImGui::TextUnformatted(file.c_str());ImGui::EndDragDropSource();
                    }
                    if(!folderItem && ImGui::BeginPopupContextItem("Asset actions")) {
                        selected=file;
                        if(ImGui::MenuItem("Attach LegacyScript to selected object",nullptr,false,world.alive(selectedObject) && AssetManager::script(file)))attachScriptAsset(world,selectedObject,file);
                        if(ImGui::MenuItem("Rename...","F2")) {selected=file;renameRequested_=true;}
                        if(ImGui::MenuItem("Move to Trash","Del")){selected=file;deleteRequested_=true;}
                        ImGui::EndPopup();
                    }
                    if(audioAsset(file) && (itemHovered || preview.playing(file))){
                        const auto cursor=ImGui::GetCursorScreenPos();
                        const bool active=preview.playing(file);
                        ImVec2 position(itemMax.x-26,itemMin.y+2);
                        ImGui::SetCursorScreenPos(position);
                        if(ImGui::InvisibleButton("##preview",ImVec2(24,24)))preview.toggle(assets,file);
                        auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(position,ImVec2(position.x+24,position.y+24),IM_COL32(40,50,65,240),3);
                        if(active)draw->AddRectFilled(ImVec2(position.x+7,position.y+7),ImVec2(position.x+17,position.y+17),IM_COL32_WHITE);
                        else draw->AddTriangleFilled(ImVec2(position.x+8,position.y+5),ImVec2(position.x+8,position.y+19),ImVec2(position.x+19,position.y+12),IM_COL32_WHITE);
                        if(ImGui::IsItemHovered())ImGui::SetTooltip(active?"Stop preview":"Play preview");
                        ImGui::SetCursorScreenPos(cursor);
                        // Submit an item after restoring layout: ImGui 1.92 rejects
                        // SetCursorScreenPos extending a child/table without an item.
                        ImGui::Dummy(ImVec2(0,0));
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
                }
                if(ImGui::BeginPopupContextWindow("Assets empty actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)) {
                    if(ImGui::MenuItem("New Folder"))createFolder();
                    if(ImGui::BeginMenu("Create")) {
                        if(ImGui::MenuItem("LegacyScript (.ls)"))createFile(".ls");
                        if(ImGui::MenuItem("Text file (.txt)"))createFile(".txt");
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
                if(shortcutFocus && !selected.empty() && !std::filesystem::is_directory(assets.resolve(selected))) {
                    if(ImGui::IsKeyPressed(ImGuiKey_F2,false))renameRequested_=true;
                    if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))deleteRequested_=true;
                }
                if(renameRequested_){
                    std::snprintf(rename_,sizeof(rename_),"%s",std::filesystem::path(selected).filename().string().c_str());
                    ImGui::SetNextWindowPos(ImGui::GetMousePos(),ImGuiCond_Appearing);
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
                if(ImGui::BeginPopup("Rename asset",ImGuiWindowFlags_AlwaysAutoResize)) {
                    if(renameFocus_){ImGui::SetKeyboardFocusHere();renameFocus_=false;}
                    const bool renameEnter=ImGui::InputText("Name",rename_,sizeof(rename_),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
                    if(ImGui::Button("Rename") || renameEnter) {
                        try {
                        std::filesystem::path leaf(rename_);
                        if(leaf.empty() || leaf.has_parent_path() || leaf==".." || leaf==".")assets.error="Use a filename without directories";
                        else {
                            auto relative=(std::filesystem::path(selected).parent_path()/leaf).generic_string();
                            if(relative==selected){ImGui::CloseCurrentPopup();}
                            else if(std::filesystem::exists(assets.resolve(relative)))assets.error="An asset with that name exists";
                            else {
                                std::filesystem::rename(assets.resolve(selected),assets.resolve(relative));
                                world.each<LuaScript>([&](Entity,LuaScript& s){if(s.path==selected)s.path=relative;for(auto& entry:s.additional)if(entry.path==selected)entry.path=relative;});
                                world.each<LuaScript>([&](Entity,LuaScript& scripts){auto rename=[&](std::vector<ScriptField>& fields){for(auto& field:fields)if(exportAssetType(field.type) && field.text==selected)field.text=relative;};rename(scripts.fields);for(auto& extra:scripts.additional)rename(extra.fields);});
                                world.each<AudioSource>([&](Entity,AudioSource& a){if(a.clip==selected)a.clip=relative;});
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
            ImGui::EndChild();
            ImGui::EndDisabled();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowContentRegionMax().x-60));
            for(int mode=0;mode<2;++mode){
                if(mode)ImGui::SameLine();
                ImGui::PushID(mode);
                auto pos=ImGui::GetCursorScreenPos();
                if(ImGui::InvisibleButton("View mode",ImVec2(26,26)))grid_=mode==1;
                auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32((grid_==(mode==1))?ImGuiCol_CheckMark:ImGuiCol_TextDisabled);
                if(mode)for(int y=0;y<2;++y)for(int x=0;x<2;++x)draw->AddRect(ImVec2(pos.x+5+x*9,pos.y+5+y*9),ImVec2(pos.x+11+x*9,pos.y+11+y*9),color);
                else for(int y=0;y<3;++y)draw->AddLine(ImVec2(pos.x+5,pos.y+6+y*6),ImVec2(pos.x+22,pos.y+6+y*6),color,2);
                if(ImGui::IsItemHovered())ImGui::SetTooltip(mode?"Grid view":"List view");
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
private:
    void createFolder(){try{selected=assets.createFolder(currentFolder);}catch(const std::exception& e){assets.error=e.what();}}
    void createFile(const std::string& extension) {
        try {
            const std::string base=extension==".ls"?"NewScript":"NewText";
            const auto prefix=std::filesystem::path(currentFolder);
            std::string name=(prefix/(base+extension)).generic_string();int n=2;
            while(std::filesystem::exists(assets.resolve(name)))name=(prefix/(base+" ("+std::to_string(n++)+")"+extension)).generic_string();
            if(extension==".ls")assets.writeScript(name,"");
            else {std::filesystem::create_directories(assets.root);std::ofstream out(assets.resolve(name));if(!out)throw std::runtime_error("Cannot create text asset");}
            selected=name;
        }catch(const std::exception& e){assets.error=e.what();}
    }
    char rename_[256]{};bool renameRequested_=false,deleteRequested_=false,grid_=false,renameFocus_=false;
};
