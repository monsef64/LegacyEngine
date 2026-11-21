#include "ui/script_gui.hpp"
#include "assets/assets.hpp"
#include "rendering/material_textures.hpp"
#include "scripting/lua_console.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <string>
#include <string_view>
#include <vector>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace {
bool drawing = false, rendering = false;
std::vector<int> scopes;
char ownerID[80]{};
int widgets = 0;
struct TableState {
    int columns = 0, specified = 0;
    bool layout = false;
};
std::vector<TableState> tableStates;
const char* names[]{"onDraw",
                    "beginWindow",
                    "endWindow",
                    "text",
                    "textWrapped",
                    "button",
                    "smallButton",
                    "checkbox",
                    "sliderFloat",
                    "sliderInt",
                    "dragFloat",
                    "inputFloat",
                    "inputInt",
                    "inputText",
                    "colorEdit3",
                    "colorEdit4",
                    "separator",
                    "sameLine",
                    "spacing",
                    "newLine",
                    "progressBar",
                    "selectable",
                    "collapsingHeader",
                    "beginChild",
                    "endChild",
                    "beginGroup",
                    "endGroup",
                    "beginDisabled",
                    "endDisabled",
                    "pushID",
                    "popID",
                    "setNextWindowPos",
                    "setNextWindowSize",
                    "setCursorPos",
                    "getWindowSize",
                    "getContentRegionAvail",
                    "setNextItemWidth",
                    "isItemHovered",
                    "isItemActive",
                    "setTooltip",
                    "bulletText",
                    "getDisplaySize",
                    "image",
                    "imageButton",
                    "beginTable",
                    "endTable",
                    "tableNextRow",
                    "tableNextColumn",
                    "tableSetColumnIndex",
                    "tableSetupColumn",
                    "tableHeadersRow",
                    "tableGetColumnIndex",
                    "tableGetColumnCount",
                    "beginMenuBar",
                    "endMenuBar",
                    "beginMenu",
                    "endMenu",
                    "menuItem",
                    "openPopup",
                    "beginPopup",
                    "beginPopupModal",
                    "endPopup",
                    "closeCurrentPopup",
                    "isPopupOpen",
                    "beginCombo",
                    "endCombo",
                    "combo",
                    "radioButton",
                    "pushStyleColor",
                    "popStyleColor",
                    "pushStyleVar",
                    "popStyleVar",
                    "pushFontSize",
                    "popFont",
                    "beginDragDropSource",
                    "endDragDropSource",
                    "setDragDropPayload",
                    "beginDragDropTarget",
                    "endDragDropTarget",
                    "acceptDragDropPayload",
                    "drawLine",
                    "drawRect",
                    "drawRectFilled",
                    "drawCircle",
                    "drawCircleFilled",
                    "drawText",
                    "drawTriangleFilled",
                    "getMousePos",
                    "isMouseDown",
                    "isMouseClicked",
                    "isMouseReleased",
                    "isKeyDown",
                    "isKeyPressed",
                    "isKeyReleased"};
const char* text(lua_State* L, int arg, std::size_t limit = 4096) {
    size_t size = 0;
    const char* value = luaL_checklstring(L, arg, &size);
    if (size > limit || std::strlen(value) != size) {
        luaL_error(L, "GUI string exceeds limit or contains NUL");
    }
    return value;
}
float number(lua_State* L, int arg, float fallback = 0) {
    const double value = luaL_optnumber(L, arg, fallback);
    if (!std::isfinite(value) || std::abs(value) > 1e7) {
        luaL_error(L, "GUI number must be finite and within +/-10000000");
    }
    return float(value);
}
void close(int kind) {
    switch (kind) {
    case 1:
        ImGui::End();
        break;
    case 2:
        ImGui::EndChild();
        break;
    case 3:
        ImGui::EndGroup();
        break;
    case 4:
        ImGui::EndDisabled();
        break;
    case 5:
        ImGui::PopID();
        break;
    case 6:
        ImGui::EndTable();
        if (!tableStates.empty()) {
            tableStates.pop_back();
        }
        break;
    case 7:
        ImGui::EndMenu();
        break;
    case 8:
        ImGui::EndMenuBar();
        break;
    case 9:
        ImGui::EndPopup();
        break;
    case 10:
        ImGui::EndCombo();
        break;
    case 11:
        ImGui::PopStyleColor();
        break;
    case 12:
        ImGui::PopStyleVar();
        break;
    case 13:
        ImGui::PopFont();
        break;
    case 14:
        ImGui::EndDragDropSource();
        break;
    case 15:
        ImGui::EndDragDropTarget();
        break;
    }
}
} // namespace
#include "ui/script_gui_advanced.inc"

ScriptGUI::~ScriptGUI() {
    shutdown();
}
void ScriptGUI::shutdown() {
    if (!context_) {
        return;
    }
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    guiImages.clear();
    if (ImGui::GetIO().BackendRendererUserData) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (ImGui::GetIO().BackendPlatformUserData) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    context_ = nullptr;
    window_ = nullptr;
    keyboard_ = mouse_ = focused_ = false;
}
void ScriptGUI::viewport(float x, float y, float w, float h, int pw, int ph) {
    x_ = x;
    y_ = y;
    w_ = std::max(w, 1.f);
    h_ = std::max(h, 1.f);
    pw_ = std::max(pw, 1);
    ph_ = std::max(ph, 1);
}
bool ScriptGUI::capturesKeyboard() const {
    return keyboard_ && focused_;
}
bool ScriptGUI::capturesMouse() const {
    return mouse_ && focused_;
}
bool ScriptGUI::event(const SDL_Event& event, SDL_Window* window, bool focused, bool locked) {
    if (!context_ || window != window_) {
        return false;
    }
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    focused_ = focused && !locked;
    SDL_Event mapped = event;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mapped.motion.x = (event.motion.x - x_) * pw_ / w_;
        mapped.motion.y = (event.motion.y - y_) * ph_ / h_;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        mx_ = (event.button.x - x_) * pw_ / w_;
        my_ = (event.button.y - y_) * ph_ / h_;
        mapped.button.x = mx_;
        mapped.button.y = my_;
    }
    ImGui_ImplSDL3_ProcessEvent(&mapped);
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mx_ = (event.motion.x - x_) * pw_ / w_;
        my_ = (event.motion.y - y_) * ph_ / h_;
    }
    const bool pointer =
        event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
    const bool key = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                     event.type == SDL_EVENT_TEXT_INPUT;
    const bool consumed = focused_ && ((pointer && mouse_) || (key && keyboard_));
    ImGui::SetCurrentContext(previous);
    return consumed;
}
void ScriptGUI::render(
    SDL_Window* window, int width, int height, bool playing, const std::filesystem::path& assets) {
    if (!playing) {
        shutdown();
        return;
    }
    auto* previous = ImGui::GetCurrentContext();
    if (!context_) {
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext()) ||
            !ImGui_ImplOpenGL3_Init("#version 330 core")) {
            SDL_Log("Runtime GUI backend initialization failed");
            ImGui::SetCurrentContext(previous);
            shutdown();
            return;
        }
        window_ = window;
    }
    ImGui::SetCurrentContext(context_);
    if (window_ != window) {
        ImGui_ImplSDL3_Shutdown();
        if (!ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext())) {
            ImGui::SetCurrentContext(previous);
            shutdown();
            return;
        }
        window_ = window;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {float(width), float(height)};
    io.DisplayFramebufferScale = {1, 1};
    const bool inside = focused_ && mx_ >= 0 && my_ >= 0 && mx_ < width && my_ < height;
    io.AddMousePosEvent(inside ? mx_ : -1e10f, inside ? my_ : -1e10f);
    io.AddFocusEvent(focused_);
    if (guiAssets.root != assets) {
        guiImages.clear();
        guiAssets.root = assets;
    }
    guiImages.beginFrame();
    ImGui::NewFrame();
    rendering = true;
    LuaConsole::drawGUI();
    rendering = false;
    ImGui::Render();
    keyboard_ = io.WantCaptureKeyboard;
    mouse_ = io.WantCaptureMouse;
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(previous);
}
bool ScriptGUI::inFrame() {
    return rendering;
}
void ScriptGUI::beginScope(std::uintptr_t owner, int listener) {
    drawing = true;
    scopes.clear();
    tableStates.clear();
    widgets = 0;
    std::snprintf(
        ownerID, sizeof(ownerID), "%llu:%d", static_cast<unsigned long long>(owner), listener);
}
void ScriptGUI::endScope() {
    while (!scopes.empty()) {
        close(scopes.back());
        scopes.pop_back();
    }
    drawing = false;
}
void ScriptGUI::install(lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < int(sizeof(names) / sizeof(*names)); ++i) {
        lua_pushinteger(L, i);
        lua_pushcclosure(L, api, 1);
        lua_setfield(L, -2, names[i]);
    }
    installGUIConstants(L);
    lua_setglobal(L, "GUI");
}
int ScriptGUI::api(lua_State* L) {
    const int op = int(lua_tointeger(L, lua_upvalueindex(1)));
    if (op == 0) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_getglobal(L, "on");
        lua_pushliteral(L, "__runtime_gui");
        lua_pushvalue(L, 1);
        lua_call(L, 2, 1);
        return 1;
    }
    if (!drawing) {
        return luaL_error(L, "GUI widgets are only available inside GUI.onDraw callbacks");
    }
    if (++widgets > 4096) {
        return luaL_error(L, "GUI widget budget exceeded");
    }
    if (op >= 42) {
        return advancedAPI(L, static_cast<ExtraOp>(op));
    }
    if (op == 1 || op == 23 || op == 25 || op == 27 || op == 29) {
        if (scopes.size() >= 32) {
            return luaL_error(L, "GUI scope depth exceeds 32");
        }
    }
    auto end = [&](int expected) {
        if (scopes.empty() || scopes.back() != expected) {
            return false;
        }
        close(expected);
        scopes.pop_back();
        return true;
    };
    // Window, child, group and ID scopes must be balanced; recovery after a Lua
    // error happens in the protected event dispatcher before the next callback.
    if (op == 2 || op == 24 || op == 26 || op == 28 || op == 30) {
        const int expected = op == 2 ? 1 : op == 24 ? 2 : op == 26 ? 3 : op == 28 ? 4 : 5;
        if (!end(expected)) {
            return luaL_error(L, "Unbalanced GUI scope end");
        }
        return 0;
    }
    if (op == 1) {
        for (int scope : scopes) {
            if (scope < 11 || scope > 13) {
                return luaL_error(L, "GUI windows cannot be nested");
            }
        }
        const char* title = text(L, 1, 256);
        char name[400];
        std::snprintf(name,
                      sizeof(name),
                      "%s###%s:%llu",
                      title,
                      ownerID,
                      static_cast<unsigned long long>(std::hash<std::string_view>{}(title)));
        const auto flags = luaL_optinteger(L, 2, 0);
        if (flags < 0 || flags > 0x7fffffff || (flags & ~allowedWindowFlags)) {
            return luaL_error(L, "Unsupported GUI window flags");
        }
        const bool visible =
            ImGui::Begin(name, nullptr, int(flags) | ImGuiWindowFlags_NoSavedSettings);
        scopes.push_back(1);
        lua_pushboolean(L, visible);
        return 1;
    }
    if (op == 31 || op == 32) {
        const float x = number(L, 1), y = number(L, 2);
        if (op == 31) {
            ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowSize({std::max(1.f, x), std::max(1.f, y)}, ImGuiCond_Always);
        }
        return 0;
    }
    if (op == 41) {
        auto size = ImGui::GetIO().DisplaySize;
        lua_pushnumber(L, size.x);
        lua_pushnumber(L, size.y);
        return 2;
    }
    if (!hasWindow()) {
        return luaL_error(L, "Call GUI.beginWindow before adding widgets");
    }
    if (op == 3 || op == 4 || op == 39 || op == 40) {
        const char* value = text(L, 1);
        if (op == 3) {
            ImGui::TextUnformatted(value);
        }
        if (op == 4) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(value);
            ImGui::PopTextWrapPos();
        }
        if (op == 39) {
            ImGui::SetTooltip("%s", value);
        }
        if (op == 40) {
            ImGui::BulletText("%s", value);
        }
        return 0;
    }
    if (op == 5 || op == 6) {
        const char* label = text(L, 1, 256);
        lua_pushboolean(L, op == 5 ? ImGui::Button(label) : ImGui::SmallButton(label));
        return 1;
    }
    if (op == 7) {
        const char* label = text(L, 1, 256);
        bool value = lua_toboolean(L, 2);
        const bool changed = ImGui::Checkbox(label, &value);
        lua_pushboolean(L, value);
        lua_pushboolean(L, changed);
        return 2;
    }
    if (op >= 8 && op <= 12) {
        const char* label = text(L, 1, 256);
        float value = number(L, 2), low = 0, high = 1, speed = .1f;
        if (op == 8 || op == 9) {
            low = number(L, 3);
            high = number(L, 4, 1);
            if (low > high) {
                return luaL_error(L, "GUI range minimum exceeds maximum");
            }
        }
        if (op == 10) {
            speed = std::max(0.f, number(L, 3, .1f));
        }
        bool changed = false;
        if (op == 8) {
            changed = ImGui::SliderFloat(label, &value, low, high);
        }
        if (op == 10) {
            changed = ImGui::DragFloat(label, &value, speed);
        }
        if (op == 11) {
            changed = ImGui::InputFloat(label, &value);
        }
        if (op == 9 || op == 12) {
            int integer = int(value);
            changed = op == 9 ? ImGui::SliderInt(label, &integer, int(low), int(high))
                              : ImGui::InputInt(label, &integer, 0);
            lua_pushinteger(L, integer);
            lua_pushboolean(L, changed);
            return 2;
        }
        lua_pushnumber(L, value);
        lua_pushboolean(L, changed);
        return 2;
    }
    if (op == 13) {
        const char* label = text(L, 1, 256);
        const char* initial = text(L, 2, 4095);
        std::array<char, 4096> buffer{};
        std::memcpy(buffer.data(), initial, std::strlen(initial));
        const bool changed = ImGui::InputText(label, buffer.data(), buffer.size());
        lua_pushstring(L, buffer.data());
        lua_pushboolean(L, changed);
        return 2;
    }
    if (op == 14 || op == 15) {
        const char* label = text(L, 1, 256);
        float color[]{number(L, 2), number(L, 3), number(L, 4), number(L, 5, 1)};
        const bool changed =
            op == 14 ? ImGui::ColorEdit3(label, color) : ImGui::ColorEdit4(label, color);
        const int count = op == 14 ? 3 : 4;
        for (int i = 0; i < count; ++i) {
            lua_pushnumber(L, color[i]);
        }
        lua_pushboolean(L, changed);
        return count + 1;
    }
    if (op == 16) {
        ImGui::Separator();
    }
    if (op == 17) {
        float offset = number(L, 1), spacing = number(L, 2, -1);
        ImGui::SameLine(offset, spacing);
    }
    if (op == 18) {
        ImGui::Spacing();
    }
    if (op == 19) {
        ImGui::NewLine();
    }
    if (op == 20) {
        const float fraction = number(L, 1);
        ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f));
    }
    if (op == 21 || op == 22) {
        const char* label = text(L, 1, 256);
        lua_pushboolean(L,
                        op == 21 ? ImGui::Selectable(label, lua_toboolean(L, 2))
                                 : ImGui::CollapsingHeader(label));
        return 1;
    }
    if (op == 23) {
        const char* label = text(L, 1, 256);
        const float w = number(L, 2), h = number(L, 3);
        const bool shown = ImGui::BeginChild(label, {w, h});
        scopes.push_back(2);
        lua_pushboolean(L, shown);
        return 1;
    }
    if (op == 25) {
        ImGui::BeginGroup();
        scopes.push_back(3);
    }
    if (op == 27) {
        ImGui::BeginDisabled(lua_isnoneornil(L, 1) || lua_toboolean(L, 1));
        scopes.push_back(4);
    }
    if (op == 29) {
        const char* id = text(L, 1, 256);
        ImGui::PushID(id);
        scopes.push_back(5);
    }
    if (op == 33) {
        const float x = number(L, 1), y = number(L, 2);
        ImGui::SetCursorPos({x, y});
    }
    if (op == 34 || op == 35) {
        const auto size = op == 34 ? ImGui::GetWindowSize() : ImGui::GetContentRegionAvail();
        lua_pushnumber(L, size.x);
        lua_pushnumber(L, size.y);
        return 2;
    }
    if (op == 36) {
        ImGui::SetNextItemWidth(number(L, 1));
    }
    if (op == 37 || op == 38) {
        lua_pushboolean(L, op == 37 ? ImGui::IsItemHovered() : ImGui::IsItemActive());
        return 1;
    }
    return 0;
}
