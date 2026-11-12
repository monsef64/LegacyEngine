#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdio>
struct EditorSettings {
    bool external=false,visible=false;
    std::string error;
    std::filesystem::path file() const {
        const char* pref=SDL_GetPrefPath("LegacyEngine","Editor");
        return pref?std::filesystem::path(pref)/"settings.txt":std::filesystem::path{};
    }
    EditorSettings(){auto path=file();if(!path.empty()){std::ifstream in(path);in>>external;}}
    void save(){auto path=file();if(!path.empty()){std::ofstream out(path);out<<external;}}
    bool openExternal(const std::filesystem::path& folder) {
        std::error_code ec;std::filesystem::create_directories(folder,ec);
        if(ec){error=ec.message();return false;}
        // URI launch avoids shell interpolation and works with the VS Code URL handler.
        auto raw=std::filesystem::absolute(folder).generic_string();std::string encoded;
        for(unsigned char c:raw){if(std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.'||c==':' )encoded+=c;else{char hex[4];std::snprintf(hex,4,"%%%02X",c);encoded+=hex;}}
        if(!SDL_OpenURL(("vscode://file/"+encoded).c_str())){error=SDL_GetError();return false;}
        error.clear();return true;
    }
    void draw(const std::filesystem::path& folder){if(!visible)return;if(ImGui::Begin("Settings",&visible)){
        ImGui::SeparatorText("Editor");int choice=external?1:0;
        if(ImGui::Combo("Code editor",&choice,"Internal\0External (VS Code)\0")){external=choice==1;save();if(external)openExternal(folder);}
        ImGui::TextWrapped("External mode opens the project folder in VS Code when a script is double-clicked. VS Code must register its vscode:// URL handler.");
        if(external && ImGui::Button("Open project in VS Code"))openExternal(folder);
        if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
    }ImGui::End();}
};
