#pragma once
#include "input/input.hpp"
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <imgui.h>
#include <fstream>
#include <iomanip>
struct InputSettings {
    bool visible=false;std::string waiting,error;char name[129]{};
    std::filesystem::path root;
    static std::filesystem::path settingsPath(const AssetManager& assets){return (assets.root.filename()=="Assets"?assets.root.parent_path():assets.root)/"Settings"/"InputSettings.json";}
    void load(const AssetManager& assets,InputState& input){
        if(root==assets.root)return;
        input.clear();root=assets.root;input.buttons.clear();waiting.clear();error.clear();
        try{
            const auto jsonPath=settingsPath(assets);
            if(std::filesystem::exists(jsonPath)){
                const auto data=readJsonFile(jsonPath,64*1024);if(data.at("version")!=1 || !data.at("buttons").is_object() || data.at("buttons").size()>256)throw std::runtime_error("Invalid input settings");
                std::map<std::string,int> next;
                for(auto it=data.at("buttons").begin();it!=data.at("buttons").end();++it){
                    auto label=it.key();if(!it.value().is_number_integer())throw std::runtime_error("Invalid input scancode");auto key=it.value().get<std::int64_t>();
                    if(label.empty() || label.size()>128 || label.find('\0')!=std::string::npos || key<=0 || key>=SDL_SCANCODE_COUNT)throw std::runtime_error("Invalid input binding");
                    next[label]=static_cast<int>(key);
                }input.buttons=std::move(next);return;
            }
            auto path=assets.resolve(".legacy-input");if(!std::filesystem::exists(path))return;
            if(std::filesystem::file_size(path)>64*1024)throw std::runtime_error("Input settings exceed 64 KiB");
            std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open input settings");std::string label;int key=0;std::map<std::string,int> next;
            while(in>>std::ws && in.peek()!=std::char_traits<char>::eof()){
                if(!(in>>std::quoted(label)>>key) || label.empty() || label.size()>128 || label.find('\0')!=std::string::npos || key<=0 || key>=SDL_SCANCODE_COUNT || next.size()>=256)throw std::runtime_error("Invalid input settings");
                next[label]=key;
            }
            if(in.bad())throw std::runtime_error("Cannot read input settings");
            input.buttons=std::move(next);
        }catch(const std::exception& e){error=e.what();}
    }
    void save(const AssetManager& assets,const InputState& input){try{
        writeJsonFile(settingsPath(assets),Json{{"version",1},{"buttons",input.buttons}});error.clear();
    }catch(const std::exception& e){error=e.what();}}
    bool capture(const SDL_Event& e,const AssetManager& assets,InputState& input){
        if(waiting.empty())return false;
        if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat){if(e.key.scancode==SDL_SCANCODE_UNKNOWN)return true;
            if(e.key.scancode!=SDL_SCANCODE_ESCAPE){input.buttons[waiting]=e.key.scancode;save(assets,input);}waiting.clear();input.clear();return true;}
        return e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP;
    }
    void draw(const AssetManager& assets,InputState& input,bool editable){
        load(assets,input);if(!visible){waiting.clear();return;}
        ImGui::SetNextWindowSize(ImVec2(520,360),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Input Settings",&visible,ImGuiWindowFlags_NoDocking)){
            ImGui::TextWrapped("Define named buttons for scripts. Choose a name, then press the key requested. Escape cancels capture.");
            ImGui::BeginDisabled(!editable || !waiting.empty());
            ImGui::InputText("Button name",name,sizeof(name));
            if(ImGui::Button("Assign key") && name[0] && input.buttons.size()<256)waiting=name;
            std::string remove;
            for(auto& pair:input.buttons){ImGui::PushID(pair.first.c_str());ImGui::Text("%s: %s",pair.first.c_str(),SDL_GetScancodeName(static_cast<SDL_Scancode>(pair.second)));ImGui::SameLine();if(ImGui::SmallButton("Rebind"))waiting=pair.first;ImGui::SameLine();if(ImGui::SmallButton("Remove"))remove=pair.first;ImGui::PopID();}
            if(!remove.empty()){input.buttons.erase(remove);save(assets,input);}
            ImGui::EndDisabled();
            if(!waiting.empty()){ImGui::Text("Press a key for '%s'...",waiting.c_str());if(ImGui::Button("Cancel capture"))waiting.clear();}
            if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
        }ImGui::End();
    }
};
