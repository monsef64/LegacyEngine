#include "script_editor.hpp"
#include <imgui.h>
#include <cstring>
#include <cctype>
#include "lua_definitions.hpp"

ScriptEditor::ScriptEditor()
{
    const char* example="-- Execute this snippet; output appears in the Lua Console.\nprint(\"Hello from Lua!\")\nfor i = 1, 3 do\n    print(\"Square\", i, i * i)\nend\n";
    std::memcpy(source_.data(),example,std::strlen(example)+1);
}

void ScriptEditor::draw(const EditorLayout* layout)
{
    const auto* viewport=ImGui::GetMainViewport();
    if(layout)placeEditorPanel(layout->lua);
    else {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,viewport->WorkPos.y+16),
                            ImGuiCond_FirstUseEver,ImVec2(1,0));
    ImGui::SetNextWindowSize(ImVec2(450,320),ImGuiCond_FirstUseEver);
    }
    if (ImGui::Begin("Lua Editor",nullptr,layout?fixedPanelFlags:0)) {
        if (ImGui::Button("Execute")) {
            console_.execute(source_.data());
            scrollToBottom_=true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Runtime")) { console_.reset(); scrollToBottom_=true; }
        ImGui::TextDisabled("Lua 5.4 | Tab: complete API names | output below");
        ImGui::InputTextMultiline("##source",source_.data(),source_.size(),ImVec2(-1,-1),
                                 ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways,
                                 completion,this);
        if (ImGui::IsItemActive() && !completionPrefix_.empty()) {
            bool opened=false;
            for (const auto& def:luaDefinitions) {
                if(std::string_view(def.name).substr(0,completionPrefix_.size())!=completionPrefix_)continue;
                if(!opened){ImGui::BeginTooltip();opened=true;}
                ImGui::TextUnformatted(def.signature);
            }
            if(opened)ImGui::EndTooltip();
        }
    }
    ImGui::End();

    if(layout)placeEditorPanel(layout->console);
    else {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,
                            viewport->WorkPos.y+viewport->WorkSize.y-16),ImGuiCond_FirstUseEver,ImVec2(1,1));
    ImGui::SetNextWindowSize(ImVec2(450,250),ImGuiCond_FirstUseEver);
    }
    if (ImGui::Begin("Lua Console",nullptr,layout?fixedPanelFlags:0)) {
        if (ImGui::Button("Clear")) console_.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll",&autoScroll_);
        ImGui::Separator();
        ImGui::BeginChild("Transcript",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
        const bool atBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1;
        for (const auto& line:console_.lines()) {
            const ImVec4 color=line.kind==LuaConsole::Kind::Error ? ImVec4(1,0.4f,0.35f,1) :
                (line.kind==LuaConsole::Kind::Info ? ImVec4(0.55f,0.75f,1,1) : ImVec4(0.9f,0.93f,0.95f,1));
            ImGui::PushStyleColor(ImGuiCol_Text,color);
            ImGui::TextUnformatted(line.text.data(),line.text.data()+line.text.size());
            ImGui::PopStyleColor();
        }
        if (scrollToBottom_ || (autoScroll_ && atBottom)) ImGui::SetScrollHereY(1);
        scrollToBottom_=false;
        ImGui::EndChild();
    }
    ImGui::End();
}

int ScriptEditor::completion(ImGuiInputTextCallbackData* data) {
    auto& self=*static_cast<ScriptEditor*>(data->UserData);
    int start=data->CursorPos;
    while(start>0 && (std::isalnum(static_cast<unsigned char>(data->Buf[start-1])) || data->Buf[start-1]=='_'))--start;
    self.completionPrefix_.assign(data->Buf+start,static_cast<std::size_t>(data->CursorPos-start));
    if(data->EventFlag==ImGuiInputTextFlags_CallbackCompletion) {
        const std::string result=luaCompletion(self.completionPrefix_);
        if(result.size()>self.completionPrefix_.size()) {
            data->DeleteChars(start,data->CursorPos-start);
            data->InsertChars(start,result.c_str());
        } else if(self.completionPrefix_.empty())data->InsertChars(data->CursorPos,"\t");
    }
    return 0;
}
