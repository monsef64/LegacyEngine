#include "editor/script_editor.hpp"
#include "editor/script_explorer.hpp"
#include "scripting/lua_definitions.hpp"
#include "scripting/lua_highlight.hpp"
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

ScriptEditor::ScriptEditor() = default;

void ScriptEditor::draw(const EditorLayout* layout,
                        bool executionEnabled,
                        AssetManager* assets,
                        std::string* openedScript) {
    const auto* viewport = ImGui::GetMainViewport();
    if (!dockedEditor || panelOpen[3]) {
        if (layout) {
            placeEditorPanel(layout->lua);
        } else {
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16, viewport->WorkPos.y + 16),
                ImGuiCond_FirstUseEver,
                ImVec2(1, 0));
            ImGui::SetNextWindowSize(ImVec2(450, 320), ImGuiCond_FirstUseEver);
        }
        if (beginEditorPanel("Script Editor", layout ? fixedPanelFlags : 0)) {
            if (assets) {
                ImGui::BeginChild(
                    "Script Explorer",
                    ImVec2(std::min(200.0f, ImGui::GetContentRegionAvail().x * .3f), 0),
                    ImGuiChildFlags_Borders);
                ImGui::TextUnformatted("Scripts");
                std::optional<std::string> createScriptIn;
                if (ImGui::Button("New Script", ImVec2(-1, 0))) {
                    createScriptIn =
                        std::filesystem::path(assetPath_).parent_path().generic_string();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Create a .ls file beside the open script, or in Assets if none is open");
                }
                try {
                    if (treeRoot_ != assets->root || ImGui::GetTime() >= treeRefresh_) {
                        auto tree = buildScriptTree(*assets);
                        scriptTree_ = std::move(tree);
                        treeRoot_ = assets->root;
                        treeRefresh_ = ImGui::GetTime() + .5;
                    }
                    if (auto chosen = scriptTree_.draw(assetPath_, "", &createScriptIn)) {
                        saveFile(*assets);
                        auto text = assets->readScript(*chosen);
                        setSource(text);
                        assetPath_ = *chosen;
                        if (openedScript) {
                            *openedScript = *chosen;
                        }
                        fileError_.clear();
                    }
                    if (ImGui::BeginPopupContextWindow("Script Explorer actions",
                                                       ImGuiPopupFlags_MouseButtonRight |
                                                           ImGuiPopupFlags_NoOpenOverItems)) {
                        if (ImGui::MenuItem("New LegacyScript in Assets")) {
                            createScriptIn = "";
                        }
                        ImGui::EndPopup();
                    }
                    if (createScriptIn) {
                        // Save the current document before switching to the newly created asset.
                        saveFile(*assets);
                        const auto folder = std::filesystem::path(*createScriptIn);
                        auto path = (folder / "NewScript.ls").generic_string();
                        for (unsigned n = 2; std::filesystem::exists(assets->resolve(path)); ++n) {
                            path = (folder / ("NewScript (" + std::to_string(n) + ").ls"))
                                       .generic_string();
                        }
                        assets->writeScript(path, "");
                        setSource("");
                        assetPath_ = path;
                        if (openedScript) {
                            *openedScript = path;
                        }
                        treeRefresh_ = 0;
                        fileError_.clear();
                    }
                } catch (const std::exception& e) {
                    fileError_ = e.what();
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("Script document");
                if ((ImGui::Button("Save") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) &&
                    fileOpen()) {
                    try {
                        saveFile(*assets);
                        fileError_.clear();
                    } catch (const std::exception& e) {
                        fileError_ = e.what();
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%s%s",
                            fileOpen() ? assetPath_.c_str() : "Select a script file",
                            fileDirty_ ? " *" : "");
                if (!fileError_.empty()) {
                    ImGui::TextWrapped("%s", fileError_.c_str());
                }
            }
            ImGui::BeginDisabled(!executionEnabled || !fileOpen());
            if (ImGui::Button("Execute")) {
                ownerError_ = !executeSource();
                scrollToBottom_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Runtime")) {
                console_.reset();
                scrollToBottom_ = true;
            }
            ImGui::EndDisabled();
            if (ownerError_) {
                ImGui::TextWrapped(
                    "Execution failed. Check Console for details. Asset scripts need a unique attachment owner or its selection.");
            }
            ImGui::TextDisabled("Lua 5.4 | Tab: complete API names | output below");
            if (!fileOpen()) {
                ImGui::TextWrapped(
                    "Open a .ls or .lua file to edit. Only script assets can be edited here.");
            }
            ImGui::BeginDisabled(!fileOpen());
            const auto sourceID = ImGui::GetID("##source");
            // Render native selection/caret/scrollbars, but not a second copy of glyphs.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            if (ImGui::InputTextMultiline("##source",
                                          source_.data(),
                                          source_.size(),
                                          ImVec2(-1, -1),
                                          ImGuiInputTextFlags_CallbackCompletion |
                                              ImGuiInputTextFlags_CallbackAlways,
                                          completion,
                                          this)) {
                fileDirty_ = true;
            }
            ImGui::PopStyleColor();
            // One colored glyph layer; ImGui retains editing, caret, selection and undo.
            auto* parent = ImGui::GetCurrentWindow();
            for (auto* child : parent->DC.ChildWindows) {
                if (child->ChildId == sourceID) {
                    auto* state = ImGui::GetInputTextState(sourceID);
                    // Use current scroll, including InputText's same-frame caret-follow
                    // adjustment (CursorStartPos still contains the pre-adjustment value).
                    const auto padding = ImGui::GetStyle().FramePadding;
                    ImVec2 origin(child->Pos.x + child->WindowPadding.x + child->DecoOuterSizeX1 +
                                      child->DC.ColumnsOffset.x - child->Scroll.x + padding.x,
                                  child->Pos.y + child->WindowPadding.y + child->DecoOuterSizeY1 -
                                      child->Scroll.y + padding.y);
                    if (state && ImGui::GetActiveID() == sourceID) {
                        origin.x -= state->Scroll.x;
                    }
                    auto* draw = child->DrawList;
                    draw->PushClipRect(child->InnerClipRect.Min, child->InnerClipRect.Max, true);
                    std::string_view text(source_.data());
                    ImU32 colors[]{ImGui::GetColorU32(ImGuiCol_Text),
                                   IM_COL32(195, 140, 245, 255),
                                   IM_COL32(210, 180, 125, 255),
                                   IM_COL32(105, 165, 110, 255),
                                   IM_COL32(120, 200, 190, 255),
                                   IM_COL32(110, 175, 240, 255)};
                    float x = origin.x, y = origin.y;
                    for (auto token : luaTokens(text)) {
                        auto pos = token.begin;
                        while (pos < token.end) {
                            auto end = text.find('\n', pos);
                            if (end == text.npos || end > token.end) {
                                end = token.end;
                            }
                            if (y + ImGui::GetFontSize() >= child->InnerClipRect.Min.y &&
                                y <= child->InnerClipRect.Max.y) {
                                draw->AddText(ImVec2(x, y),
                                              colors[token.kind],
                                              text.data() + pos,
                                              text.data() + end);
                            }
                            x += ImGui::GetFont()
                                     ->CalcTextSizeA(ImGui::GetFontSize(),
                                                     FLT_MAX,
                                                     0,
                                                     text.data() + pos,
                                                     text.data() + end)
                                     .x;
                            pos = end;
                            if (pos < token.end && text[pos] == '\n') {
                                ++pos;
                                x = origin.x;
                                y += ImGui::GetFontSize();
                            }
                        }
                    }
                    draw->PopClipRect();
                    break;
                }
            }
            if (ImGui::IsItemActive() && !completionPrefix_.empty()) {
                bool opened = false;
                for (const auto& def : luaDefinitions) {
                    if (std::string_view(def.name).substr(0, completionPrefix_.size()) !=
                        completionPrefix_) {
                        continue;
                    }
                    if (!opened) {
                        ImGui::BeginTooltip();
                        opened = true;
                    }
                    ImGui::TextUnformatted(def.signature);
                }
                if (opened) {
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndDisabled();
            if (assets) {
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }
    if (!dockedEditor || panelOpen[4]) {
        if (layout) {
            placeEditorPanel(layout->console);
        } else {
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16,
                                           viewport->WorkPos.y + viewport->WorkSize.y - 16),
                                    ImGuiCond_FirstUseEver,
                                    ImVec2(1, 1));
            ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
        }
        if (beginEditorPanel("Console", layout ? fixedPanelFlags : 0)) {
            if (ImGui::Button("Clear")) {
                console_.clear();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &autoScroll_);
            ImGui::SameLine();
            if (ImGui::Button("Copy all")) {
                std::string text;
                for (const auto& line : console_.lines()) {
                    text += line.text + "\n";
                }
                for (const auto& line : componentOutput) {
                    text += line + "\n";
                }
                ImGui::SetClipboardText(text.c_str());
            }
            ImGui::Separator();
            ImGui::BeginChild("Transcript",
                              ImVec2(0, 0),
                              ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
            int logIndex = 0;
            for (const auto& line : console_.lines()) {
                ImGui::PushID(logIndex++);
                const ImVec4 color =
                    line.kind == LuaConsole::Kind::Error
                        ? ImVec4(1, 0.4f, 0.35f, 1)
                        : (line.kind == LuaConsole::Kind::Warning ? ImVec4(1, .78f, .3f, 1)
                           : line.kind == LuaConsole::Kind::Info  ? ImVec4(0.55f, 0.75f, 1, 1)
                                                                  : ImVec4(0.9f, 0.93f, 0.95f, 1));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(line.text.data(), line.text.data() + line.text.size());
                if (ImGui::BeginPopupContextItem("Message", ImGuiPopupFlags_MouseButtonRight)) {
                    if (ImGui::MenuItem("Copy message")) {
                        ImGui::SetClipboardText(line.text.c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::PushID("ComponentLogs");
            for (std::size_t i = 0; i < componentOutput.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextUnformatted(componentOutput[i].c_str());
                if (ImGui::BeginPopupContextItem("Message", ImGuiPopupFlags_MouseButtonRight)) {
                    if (ImGui::MenuItem("Copy message")) {
                        ImGui::SetClipboardText(componentOutput[i].c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            if (scrollToBottom_ || (autoScroll_ && atBottom)) {
                ImGui::SetScrollHereY(1);
            }
            scrollToBottom_ = false;
            ImGui::EndChild();
        }
        ImGui::End();
    }
}

int ScriptEditor::completion(ImGuiInputTextCallbackData* data) {
    auto& self = *static_cast<ScriptEditor*>(data->UserData);
    int start = data->CursorPos;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(data->Buf[start - 1])) ||
                         data->Buf[start - 1] == '_')) {
        --start;
    }
    self.completionPrefix_.assign(data->Buf + start,
                                  static_cast<std::size_t>(data->CursorPos - start));
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        const std::string result = luaCompletion(self.completionPrefix_);
        if (result.size() > self.completionPrefix_.size()) {
            data->DeleteChars(start, data->CursorPos - start);
            data->InsertChars(start, result.c_str());
        } else if (self.completionPrefix_.empty()) {
            data->InsertChars(data->CursorPos, "\t");
        }
    }
    return 0;
}
