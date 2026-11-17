#include "editor/script_editor.hpp"
#include "editor/script_explorer.hpp"
#include "scripting/lua_definitions.hpp"
#include "scripting/lua_highlight.hpp"
#include <cctype>
#include <cstring>
#include "ui/mygui_ui.hpp"

void script_editor_detail::drawLuaSyntaxOverlay(ui::DrawList* draw,
                                               std::string_view source,
                                               const ui::MultilineEditInfo& editInfo) {
    if (!draw) return;
    draw->pushClipRect(editInfo.innerRect.min(), editInfo.innerRect.max(), true);
    const uint32_t colors[]{ui::colorU32(ui::Col_Text),
                            UI_COL32(195, 140, 245, 255),
                            UI_COL32(210, 180, 125, 255),
                            UI_COL32(105, 165, 110, 255),
                            UI_COL32(120, 200, 190, 255),
                            UI_COL32(110, 175, 240, 255)};
    const auto origin = editInfo.textOrigin;
    float x = origin.x - editInfo.scrollX;
    float y = origin.y - editInfo.scrollY;
    const float glyphHeight = ui::fontSize();
    for (const auto token : luaTokens(source)) {
        std::size_t pos = token.begin;
        while (pos < token.end) {
            auto lineEnd = source.find('\n', pos);
            if (lineEnd == source.npos || lineEnd > token.end) lineEnd = token.end;
            if (y + glyphHeight >= editInfo.innerRect.y &&
                y <= editInfo.innerRect.y + editInfo.innerRect.height) {
                draw->addText(glyphHeight,
                              {x, y},
                              colors[token.kind],
                              source.data() + pos,
                              source.data() + lineEnd);
            }
            const std::string line(source.data() + pos, lineEnd - pos);
            x += ui::calcTextSize(line.c_str()).x;
            pos = lineEnd;
            if (pos < token.end && source[pos] == '\n') {
                ++pos;
                x = origin.x - editInfo.scrollX;
                y += glyphHeight;
            }
        }
    }
    draw->popClipRect();
}

ScriptEditor::ScriptEditor() = default;

void ScriptEditor::draw(const EditorLayout* layout,
                        bool executionEnabled,
                        AssetManager* assets,
                        std::string* openedScript) {
    const auto* viewport = ui::mainViewport();
    if (!dockedEditor || panelOpen[3]) {
        if (layout) {
            placeEditorPanel(layout->lua);
        } else {
            ui::setNextWindowPos(
                ui::Vec2(viewport->WorkPos.x + viewport->WorkSize.x - 16, viewport->WorkPos.y + 16),
                ui::Cond_FirstUseEver,
                ui::Vec2(1, 0));
            ui::setNextWindowSize(ui::Vec2(450, 320), ui::Cond_FirstUseEver);
        }
        if (beginEditorPanel("Script Editor", layout ? fixedPanelFlags : 0)) {
            if (assets) {
                ui::beginChild(
                    "Script Explorer",
                    ui::Vec2(std::min(200.0f, ui::contentRegionAvail().x * .3f), 0),
                    ui::ChildFlags_Borders);
                ui::textUnformatted("Scripts");
                std::optional<std::string> createScriptIn;
                if (ui::button("New Script", ui::Vec2(-1, 0))) {
                    createScriptIn =
                        std::filesystem::path(assetPath_).parent_path().generic_string();
                }
                if (ui::isItemHovered()) {
                    ui::setTooltip(
                        "Create a .ls file beside the open script, or in Assets if none is open");
                }
                try {
                    if (treeRoot_ != assets->root || ui::timeSeconds() >= treeRefresh_) {
                        auto tree = buildScriptTree(*assets);
                        scriptTree_ = std::move(tree);
                        treeRoot_ = assets->root;
                        treeRefresh_ = ui::timeSeconds() + .5;
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
                    if (ui::beginPopupContextWindow("Script Explorer actions",
                                                       ui::PopupFlags_MouseButtonRight |
                                                           ui::PopupFlags_NoOpenOverItems)) {
                        if (ui::menuItem("New LegacyScript in Assets")) {
                            createScriptIn = "";
                        }
                        ui::endPopup();
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
                ui::endChild();
                ui::sameLine();
                ui::beginChild("Script document");
                if ((ui::button("Save") || ui::shortcut(ui::Mod_Ctrl | ui::Key_S)) &&
                    fileOpen()) {
                    try {
                        saveFile(*assets);
                        fileError_.clear();
                    } catch (const std::exception& e) {
                        fileError_ = e.what();
                    }
                }
                ui::sameLine();
                ui::text("%s%s",
                            fileOpen() ? assetPath_.c_str() : "Select a script file",
                            fileDirty_ ? " *" : "");
                if (!fileError_.empty()) {
                    ui::textWrapped("%s", fileError_.c_str());
                }
            }
            ui::beginDisabled(!executionEnabled || !fileOpen());
            if (ui::button("Execute")) {
                ownerError_ = !executeSource();
                scrollToBottom_ = true;
            }
            ui::sameLine();
            if (ui::button("Reset Runtime")) {
                console_.reset();
                scrollToBottom_ = true;
            }
            ui::endDisabled();
            if (ownerError_) {
                ui::textWrapped(
                    "Execution failed. Check Console for details. Asset scripts need a unique attachment owner or its selection.");
            }
            ui::textDisabled("Lua 5.4 | Tab: complete API names | output below");
            if (!fileOpen()) {
                ui::textWrapped(
                    "Open a .ls or .lua file to edit. Only script assets can be edited here.");
            }
            ui::beginDisabled(!fileOpen());
            ui::MultilineEditInfo editInfo;
            ui::pushStyleColor(ui::Col_Text, ui::Color(0, 0, 0, 0));
            const bool sourceChanged = ui::inputTextMultiline("##source",
                                                              source_.data(),
                                                              source_.size(),
                                                              ui::Vec2(-1, -1),
                                                              ui::InputTextFlags_CallbackCompletion |
                                                                  ui::InputTextFlags_CallbackAlways,
                                                              completion,
                                                              this,
                                                              &editInfo);
            ui::popStyleColor();
            if (sourceChanged) fileDirty_ = true;

            // MyGUI's EditBox owns caret, selection, text input, and scrolling.
            // A clipped native draw-list pass adds Lua token colours above it.
            if (editInfo.drawList) {
                script_editor_detail::drawLuaSyntaxOverlay(
                    editInfo.drawList, source_.data(), editInfo);
            }
            if (ui::isItemActive() && !completionPrefix_.empty()) {
                bool opened = false;
                for (const auto& def : luaDefinitions) {
                    if (std::string_view(def.name).substr(0, completionPrefix_.size()) !=
                        completionPrefix_) {
                        continue;
                    }
                    if (!opened) {
                        ui::beginTooltip();
                        opened = true;
                    }
                    ui::textUnformatted(def.signature);
                }
                if (opened) {
                    ui::endTooltip();
                }
            }
            ui::endDisabled();
            if (assets) {
                ui::endChild();
            }
        }
        ui::endWindow();
    }
    if (!dockedEditor || panelOpen[4]) {
        if (layout) {
            placeEditorPanel(layout->console);
        } else {
            ui::setNextWindowPos(ui::Vec2(viewport->WorkPos.x + viewport->WorkSize.x - 16,
                                           viewport->WorkPos.y + viewport->WorkSize.y - 16),
                                    ui::Cond_FirstUseEver,
                                    ui::Vec2(1, 1));
            ui::setNextWindowSize(ui::Vec2(450, 250), ui::Cond_FirstUseEver);
        }
        if (beginEditorPanel("Console", layout ? fixedPanelFlags : 0)) {
            if (ui::button("Clear")) {
                console_.clear();
            }
            ui::sameLine();
            ui::checkbox("Auto-scroll", &autoScroll_);
            ui::sameLine();
            if (ui::button("Copy all")) {
                std::string text;
                for (const auto& line : console_.lines()) {
                    text += line.text + "\n";
                }
                for (const auto& line : componentOutput) {
                    text += line + "\n";
                }
                ui::setClipboardText(text.c_str());
            }
            ui::separator();
            ui::beginChild("Transcript",
                              ui::Vec2(0, 0),
                              ui::ChildFlags_None,
                              ui::WindowFlags_HorizontalScrollbar);
            const bool atBottom = ui::scrollY() >= ui::scrollMaxY() - 1;
            int logIndex = 0;
            for (const auto& line : console_.lines()) {
                ui::pushId(logIndex++);
                const ui::Color color =
                    line.kind == LuaConsole::Kind::Error
                        ? ui::Color(1, 0.4f, 0.35f, 1)
                        : (line.kind == LuaConsole::Kind::Warning ? ui::Color(1, .78f, .3f, 1)
                           : line.kind == LuaConsole::Kind::Info  ? ui::Color(0.55f, 0.75f, 1, 1)
                                                                  : ui::Color(0.9f, 0.93f, 0.95f, 1));
                ui::pushStyleColor(ui::Col_Text, color);
                ui::textUnformatted(line.text.data(), line.text.data() + line.text.size());
                if (ui::beginPopupContextItem("Message", ui::PopupFlags_MouseButtonRight)) {
                    if (ui::menuItem("Copy message")) {
                        ui::setClipboardText(line.text.c_str());
                    }
                    ui::endPopup();
                }
                ui::popStyleColor();
                ui::popId();
            }
            ui::pushId("ComponentLogs");
            for (std::size_t i = 0; i < componentOutput.size(); ++i) {
                ui::pushId(static_cast<int>(i));
                ui::textUnformatted(componentOutput[i].c_str());
                if (ui::beginPopupContextItem("Message", ui::PopupFlags_MouseButtonRight)) {
                    if (ui::menuItem("Copy message")) {
                        ui::setClipboardText(componentOutput[i].c_str());
                    }
                    ui::endPopup();
                }
                ui::popId();
            }
            ui::popId();
            if (scrollToBottom_ || (autoScroll_ && atBottom)) {
                ui::setScrollHereY(1);
            }
            scrollToBottom_ = false;
            ui::endChild();
        }
        ui::endWindow();
    }
}

int ScriptEditor::completion(ui::InputTextCallbackData* data) {
    auto& self = *static_cast<ScriptEditor*>(data->userData);
    int start = data->cursorPos;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(data->buf[start - 1])) ||
                         data->buf[start - 1] == '_')) {
        --start;
    }
    self.completionPrefix_.assign(data->buf + start,
                                  static_cast<std::size_t>(data->cursorPos - start));
    if (data->eventFlag == ui::InputTextFlags_CallbackCompletion) {
        const std::string result = luaCompletion(self.completionPrefix_);
        if (result.size() > self.completionPrefix_.size()) {
            data->deleteChars(start, data->cursorPos - start);
            data->insertChars(start, result.c_str());
        } else if (self.completionPrefix_.empty()) {
            data->insertChars(data->cursorPos, "\t");
        }
    }
    return 0;
}
