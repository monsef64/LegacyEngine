#include "script_editor.hpp"
#include <imgui.h>
#include <cstring>

ScriptEditor::ScriptEditor()
{
    const char* example="-- Execute this snippet; output appears in the Lua Console.\nprint(\"Hello from Lua!\")\nfor i = 1, 3 do\n    print(\"Square\", i, i * i)\nend\n";
    std::memcpy(source_.data(),example,std::strlen(example)+1);
}

void ScriptEditor::draw()
{
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,viewport->WorkPos.y+16),
                            ImGuiCond_FirstUseEver,ImVec2(1,0));
    ImGui::SetNextWindowSize(ImVec2(450,320),ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lua Editor")) {
        if (ImGui::Button("Execute")) {
            console_.execute(source_.data());
            scrollToBottom_=true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Runtime")) { console_.reset(); scrollToBottom_=true; }
        ImGui::TextDisabled("Lua 5.4 | 64 KiB source | output below");
        ImGui::InputTextMultiline("##source",source_.data(),source_.size(),ImVec2(-1,-1),
                                 ImGuiInputTextFlags_AllowTabInput);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,
                            viewport->WorkPos.y+viewport->WorkSize.y-16),ImGuiCond_FirstUseEver,ImVec2(1,1));
    ImGui::SetNextWindowSize(ImVec2(450,250),ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lua Console")) {
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
