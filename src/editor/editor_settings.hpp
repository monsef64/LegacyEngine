#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>
#include "editor/editor_palette.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdio>
struct EditorSettings {
    bool external=false,visible=false;int theme=0;
    std::string error;
    std::filesystem::path file() const {
        const char* pref=SDL_GetPrefPath("LegacyEngine","Editor");
        return pref?std::filesystem::path(pref)/"settings.txt":std::filesystem::path{};
    }
    EditorSettings(){auto path=file();if(!path.empty()){std::ifstream in(path);in>>external;if(!(in>>theme))theme=0;theme=std::clamp(theme,0,1);}if(ImGui::GetCurrentContext())applyEditorPalette(theme);}
    void save(){auto path=file();if(!path.empty()){std::ofstream out(path);out<<external<<' '<<theme;}}
    bool openExternal(const std::filesystem::path& folder,bool isFile=false) {
        std::error_code ec;std::filesystem::create_directories(isFile?folder.parent_path():folder,ec);
        if(ec){error=ec.message();return false;}
        // URI launch avoids shell interpolation and works with the VS Code URL handler.
        auto raw=std::filesystem::absolute(folder).generic_string();std::string encoded;
        for(unsigned char c:raw){if(std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.'||c==':' )encoded+=c;else{char hex[4];std::snprintf(hex,4,"%%%02X",c);encoded+=hex;}}
        if(!SDL_OpenURL(("vscode://file/"+encoded).c_str())){error=SDL_GetError();return false;}
        error.clear();return true;
    }
    void draw(const std::filesystem::path& folder){if(!visible)return;
        const auto area=ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(ImVec2(std::min(540.0f,area.x),std::min(300.0f,area.y)),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(360.0f,area.x),std::min(220.0f,area.y)),area);
        if(ImGui::Begin("Settings",&visible,ImGuiWindowFlags_NoDocking)){
        ImGui::SetNextItemWidth(-120);
        if(ImGui::Combo("Theme",&theme,"Legacy Dark\0Grey\0")){applyEditorPalette(theme);save();}
        ImGui::SeparatorText("Editor");int choice=external?1:0;
        ImGui::SetNextItemWidth(-120);
        if(ImGui::Combo("Code editor",&choice,"Internal\0External (VS Code)\0")){external=choice==1;save();if(external)openExternal(folder);}
        ImGui::TextWrapped("External mode opens the selected script in VS Code when double-clicked. VS Code must register its vscode:// URL handler.");
        if(external && ImGui::Button("Open project in VS Code"))openExternal(folder);
        if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
    }ImGui::End();}
};
