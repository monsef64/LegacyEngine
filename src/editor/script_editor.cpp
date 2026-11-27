#include "editor/script_editor.hpp"
#include "editor/asset_browser.hpp"
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
                        std::string* openedScript,
                        AssetBrowser* browser,
                        bool fileOperationsEnabled,
                        EditorHistory* history) {
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
                ScriptTreeAction scriptAction;
                ImGui::TextUnformatted("Assets");
                scriptFolderDrop("", browser && fileOperationsEnabled ? &scriptAction : nullptr);
                std::optional<std::string> createScriptIn;
                if (ImGui::Button("New Script", ImVec2(-1, 0))) {
                    createScriptIn =
                        std::filesystem::path(assetPath_).parent_path().generic_string();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Create a .lua file beside the open script, or in Assets if none is open");
                }
                try {
                    if (treeRoot_ != assets->root || ImGui::GetTime() >= treeRefresh_) {
                        auto tree = buildScriptTree(*assets);
                        scriptTree_ = std::move(tree);
                        treeRoot_ = assets->root;
                        treeRefresh_ = ImGui::GetTime() + .5;
                    }
                    if (auto chosen = scriptTree_.draw(
                            assetPath_,
                            "",
                            &createScriptIn,
                            browser && fileOperationsEnabled ? &scriptAction : nullptr)) {
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
                    if (fileOperationsEnabled && browser && world_ &&
                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2) &&
                        fileOpen()) {
                        scriptAction = {ScriptTreeAction::Rename, assetPath_, {}};
                    }
                    if (fileOperationsEnabled && browser && world_ &&
                        scriptAction.kind != ScriptTreeAction::None) {
                        if (!AssetManager::script(scriptAction.path) ||
                            !std::filesystem::is_regular_file(assets->resolve(scriptAction.path))) {
                            throw std::runtime_error("Select an existing script");
                        }
                        saveFile(*assets);
                        scriptActionPath_ = scriptAction.path;
                        scriptActionRoot_ = assets->root;
                        fileError_.clear();
                        if (scriptAction.kind == ScriptTreeAction::Rename) {
                            std::snprintf(scriptRename_,
                                          sizeof(scriptRename_),
                                          "%s",
                                          std::filesystem::path(scriptAction.path)
                                              .filename()
                                              .string()
                                              .c_str());
                            scriptRenameFocus_ = true;
                            ImGui::OpenPopup("Rename script");
                        } else if (scriptAction.kind == ScriptTreeAction::Delete) {
                            ImGui::OpenPopup("Delete script?");
                        } else if (scriptAction.kind == ScriptTreeAction::Duplicate) {
                            browser->selected = scriptAction.path;
                            browser->copySelected(false);
                            browser->pasteAssets(*world_,
                                                 std::filesystem::path(scriptAction.path)
                                                     .parent_path()
                                                     .generic_string());
                            treeRefresh_ = 0;
                        } else if (scriptAction.kind == ScriptTreeAction::Move) {
                            const auto destination = assets->resolve(scriptAction.folder);
                            if (!std::filesystem::is_directory(destination)) {
                                throw std::runtime_error("Drop onto an existing folder");
                            }
                            browser->selected = scriptAction.path;
                            browser->renameSelected(
                                *world_,
                                (std::filesystem::path(scriptAction.folder) /
                                 std::filesystem::path(scriptAction.path).filename())
                                    .generic_string(),
                                true);
                            if (assetPath_ == scriptAction.path) {
                                assetPath_ = browser->selected;
                                if (openedScript) {
                                    *openedScript = assetPath_;
                                }
                            }
                            treeRefresh_ = 0;
                        }
                    }
                    if (fileOperationsEnabled && createScriptIn) {
                        // Save the current document before switching to the newly created asset.
                        saveFile(*assets);
                        const auto folder = std::filesystem::path(*createScriptIn);
                        auto path = (folder / "NewScript.lua").generic_string();
                        for (unsigned n = 2; std::filesystem::exists(assets->resolve(path)); ++n) {
                            path = (folder / ("NewScript (" + std::to_string(n) + ").lua"))
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
                if (ImGui::BeginPopupModal(
                        "Rename script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (scriptRenameFocus_) {
                        ImGui::SetKeyboardFocusHere();
                        scriptRenameFocus_ = false;
                    }
                    const bool enter = ImGui::InputText("Name",
                                                        scriptRename_,
                                                        sizeof(scriptRename_),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
                    if (fileOperationsEnabled && (ImGui::Button("Rename") || enter)) {
                        try {
                            if (scriptActionRoot_ != assets->root) {
                                throw std::runtime_error(
                                    "Project changed; select the script again");
                            }
                            saveFile(*assets);
                            browser->selected = scriptActionPath_;
                            browser->renameSelected(*world_, scriptRename_);
                            if (assetPath_ == scriptActionPath_) {
                                assetPath_ = browser->selected;
                                if (openedScript) {
                                    *openedScript = assetPath_;
                                }
                            }
                            treeRefresh_ = 0;
                            fileError_.clear();
                            ImGui::CloseCurrentPopup();
                        } catch (const std::exception& ex) {
                            fileError_ = ex.what();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    if (!fileError_.empty()) {
                        ImGui::TextWrapped("%s", fileError_.c_str());
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::BeginPopupModal(
                        "Delete script?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped(
                        "Move %s to Trash? Scene references will remain missing until restored or reassigned. Undo restores the file.",
                        scriptActionPath_.c_str());
                    if (fileOperationsEnabled && ImGui::Button("Delete")) {
                        try {
                            saveFile(*assets);
                            if (scriptActionRoot_ != assets->root) {
                                throw std::runtime_error(
                                    "Project changed; select the script again");
                            }
                            if (!history || !world_) {
                                throw std::runtime_error("Script deletion requires editor history");
                            }
                            auto receipt =
                                std::make_shared<TrashFile>(assets->resolve(scriptActionPath_));
                            if (!history->performAsset(*world_, selected_, [receipt](bool redo) {
                                    if (redo) {
                                        receipt->remove();
                                    } else {
                                        receipt->restore();
                                    }
                                    ++modelAssetRevision;
                                })) {
                                throw std::runtime_error(history->error);
                            }
                            if (assetPath_ == scriptActionPath_) {
                                setSource("");
                                assetPath_.clear();
                                if (openedScript) {
                                    openedScript->clear();
                                }
                            }
                            if (browser->selected == scriptActionPath_) {
                                browser->selected.clear();
                            }
                            treeRefresh_ = 0;
                            fileError_.clear();
                            ImGui::CloseCurrentPopup();
                        } catch (const std::exception& ex) {
                            fileError_ = ex.what();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    if (!fileError_.empty()) {
                        ImGui::TextWrapped("%s", fileError_.c_str());
                    }
                    ImGui::EndPopup();
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
            const float gutter = ImGui::CalcTextSize("00000").x + 12;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ImGui::GetStyle().FramePadding.x + gutter,
                                       ImGui::GetStyle().FramePadding.y));
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
            ImGui::PopStyleVar();
            // One colored glyph layer; ImGui retains editing, caret, selection and undo.
            auto* parent = ImGui::GetCurrentWindow();
            for (auto* child : parent->DC.ChildWindows) {
                if (child->ChildId == sourceID) {
                    auto* state = ImGui::GetInputTextState(sourceID);
                    // Use current scroll, including InputText's same-frame caret-follow
                    // adjustment (CursorStartPos still contains the pre-adjustment value).
                    const auto padding = ImVec2(ImGui::GetStyle().FramePadding.x + gutter,
                                                ImGui::GetStyle().FramePadding.y);
                    ImVec2 origin(child->Pos.x + child->WindowPadding.x + child->DecoOuterSizeX1 +
                                      child->DC.ColumnsOffset.x - child->Scroll.x + padding.x,
                                  child->Pos.y + child->WindowPadding.y + child->DecoOuterSizeY1 -
                                      child->Scroll.y + padding.y);
                    if (state && ImGui::GetActiveID() == sourceID) {
                        origin.x -= state->Scroll.x;
                    }
                    auto* draw = child->DrawList;
                    draw->PushClipRect(child->InnerClipRect.Min, child->InnerClipRect.Max, true);
                    draw->AddRectFilled(
                        child->InnerClipRect.Min,
                        ImVec2(child->InnerClipRect.Min.x + gutter, child->InnerClipRect.Max.y),
                        IM_COL32(25, 29, 35, 255));
                    std::string_view text(source_.data());
                    int line = 1;
                    float lineY = origin.y;
                    for (std::size_t i = 0; i <= text.size(); ++i) {
                        if (i == 0 || text[i - 1] == '\n') {
                            if (lineY + ImGui::GetFontSize() >= child->InnerClipRect.Min.y &&
                                lineY <= child->InnerClipRect.Max.y) {
                                char number[24];
                                std::snprintf(number, sizeof(number), "%d", line);
                                draw->AddText({child->InnerClipRect.Min.x + gutter -
                                                   ImGui::CalcTextSize(number).x - 6,
                                               lineY},
                                              IM_COL32(125, 140, 155, 255),
                                              number);
                            }
                            ++line;
                            lineY += ImGui::GetFontSize();
                        }
                    }
                    draw->PopClipRect();
                    draw->PushClipRect(
                        ImVec2(child->InnerClipRect.Min.x + gutter, child->InnerClipRect.Min.y),
                        child->InnerClipRect.Max,
                        true);
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
                componentOutput.clear();
                clearComponentOutputRequested = true;
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
