#include "editor/script_editor.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include "scripting/lua_highlight.hpp"
#include <cstring>
#include <cctype>
#include "scripting/lua_definitions.hpp"

ScriptEditor::ScriptEditor() = default;

void ScriptEditor::draw(const EditorLayout* layout, bool executionEnabled)
{
    const auto* viewport=ImGui::GetMainViewport();
    if(!dockedEditor || panelOpen[3]) {
    if(layout)placeEditorPanel(layout->lua);
    else {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,viewport->WorkPos.y+16),
                            ImGuiCond_FirstUseEver,ImVec2(1,0));
    ImGui::SetNextWindowSize(ImVec2(450,320),ImGuiCond_FirstUseEver);
    }
    if (beginEditorPanel("Lua Editor",layout?fixedPanelFlags:0)) {
        ImGui::BeginDisabled(!executionEnabled);
        if (ImGui::Button("Execute")) {
            ownerError_=!executeSource();
            scrollToBottom_=true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Runtime")) { console_.reset(); scrollToBottom_=true; }
        ImGui::EndDisabled();
        if(ownerError_)ImGui::TextWrapped("Select a GameObject with this script attached before executing it.");
        ImGui::TextDisabled("Lua 5.4 | Tab: complete API names | output below");
        const auto sourceID=ImGui::GetID("##source");
        ImGui::InputTextMultiline("##source",source_.data(),source_.size(),ImVec2(-1,-1),
                                 ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways,
                                 completion,this);
        // Color tokens over the native input rendering; keep its caret, selection and undo.
        auto* parent=ImGui::GetCurrentWindow();
        for(auto* child:parent->DC.ChildWindows)if(child->ChildId==sourceID){
            auto* state=ImGui::GetInputTextState(sourceID);
            auto origin=child->DC.CursorStartPos;
            if(state)origin.x-=state->Scroll.x;
            auto* draw=child->DrawList;draw->PushClipRect(child->InnerClipRect.Min,child->InnerClipRect.Max,true);
            std::string_view text(source_.data());
            ImU32 colors[]{ImGui::GetColorU32(ImGuiCol_Text),IM_COL32(195,140,245,255),IM_COL32(210,180,125,255),IM_COL32(105,165,110,255),IM_COL32(120,200,190,255),IM_COL32(110,175,240,255)};
            float x=origin.x,y=origin.y;
            for(auto token:luaTokens(text)){
                auto pos=token.begin;
                while(pos<token.end){auto end=text.find('\n',pos);if(end==text.npos || end>token.end)end=token.end;
                    if(token.kind!=LuaToken::Plain && y+ImGui::GetFontSize()>=child->InnerClipRect.Min.y && y<=child->InnerClipRect.Max.y)
                        draw->AddText(ImVec2(x,y),colors[token.kind],text.data()+pos,text.data()+end);
                    x+=ImGui::CalcTextSize(text.data()+pos,text.data()+end).x;
                    pos=end;if(pos<token.end && text[pos]=='\n'){++pos;x=origin.x;y+=ImGui::GetFontSize();}
                }
            }
            draw->PopClipRect();break;
        }
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

    }
    if(!dockedEditor || panelOpen[4]) {
    if(layout)placeEditorPanel(layout->console);
    else {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x-16,
                            viewport->WorkPos.y+viewport->WorkSize.y-16),ImGuiCond_FirstUseEver,ImVec2(1,1));
    ImGui::SetNextWindowSize(ImVec2(450,250),ImGuiCond_FirstUseEver);
    }
    if (beginEditorPanel("Lua Console",layout?fixedPanelFlags:0)) {
        if (ImGui::Button("Clear")) console_.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll",&autoScroll_);ImGui::SameLine();
        if(ImGui::Button("Copy all")){std::string text;for(const auto& line:console_.lines())text+=line.text+"\n";for(const auto& line:componentOutput)text+=line+"\n";ImGui::SetClipboardText(text.c_str());}
        ImGui::Separator();
        ImGui::BeginChild("Transcript",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
        const bool atBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1;
        int logIndex=0;
        for (const auto& line:console_.lines()) {
            ImGui::PushID(logIndex++);
            const ImVec4 color=line.kind==LuaConsole::Kind::Error ? ImVec4(1,0.4f,0.35f,1) :
                (line.kind==LuaConsole::Kind::Warning ? ImVec4(1,.78f,.3f,1) : line.kind==LuaConsole::Kind::Info ? ImVec4(0.55f,0.75f,1,1) : ImVec4(0.9f,0.93f,0.95f,1));
            ImGui::PushStyleColor(ImGuiCol_Text,color);
            ImGui::TextUnformatted(line.text.data(),line.text.data()+line.text.size());
            if(ImGui::BeginPopupContextItem("Message",ImGuiPopupFlags_MouseButtonRight)){if(ImGui::MenuItem("Copy message"))ImGui::SetClipboardText(line.text.c_str());ImGui::EndPopup();}
            ImGui::PopStyleColor();ImGui::PopID();
        }
        ImGui::PushID("ComponentLogs");
        for(std::size_t i=0;i<componentOutput.size();++i){ImGui::PushID(static_cast<int>(i));ImGui::TextUnformatted(componentOutput[i].c_str());if(ImGui::BeginPopupContextItem("Message",ImGuiPopupFlags_MouseButtonRight)){if(ImGui::MenuItem("Copy message"))ImGui::SetClipboardText(componentOutput[i].c_str());ImGui::EndPopup();}ImGui::PopID();}
        ImGui::PopID();
        if (scrollToBottom_ || (autoScroll_ && atBottom)) ImGui::SetScrollHereY(1);
        scrollToBottom_=false;
        ImGui::EndChild();
    }
    ImGui::End();
    }
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
