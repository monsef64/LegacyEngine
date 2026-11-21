namespace {
enum class ExtraOp {
    image = 42,
    imageButton,
    beginTable,
    endTable,
    tableNextRow,
    tableNextColumn,
    tableSetColumnIndex,
    tableSetupColumn,
    tableHeadersRow,
    tableGetColumnIndex,
    tableGetColumnCount,
    beginMenuBar,
    endMenuBar,
    beginMenu,
    endMenu,
    menuItem,
    openPopup,
    beginPopup,
    beginPopupModal,
    endPopup,
    closeCurrentPopup,
    isPopupOpen,
    beginCombo,
    endCombo,
    combo,
    radioButton,
    pushStyleColor,
    popStyleColor,
    pushStyleVar,
    popStyleVar,
    pushFontSize,
    popFont,
    beginDragDropSource,
    endDragDropSource,
    setDragDropPayload,
    beginDragDropTarget,
    endDragDropTarget,
    acceptDragDropPayload,
    drawLine,
    drawRect,
    drawRectFilled,
    drawCircle,
    drawCircleFilled,
    drawText,
    drawTriangleFilled,
    getMousePos,
    isMouseDown,
    isMouseClicked,
    isMouseReleased,
    isKeyDown,
    isKeyPressed,
    isKeyReleased
};
MaterialTextureCache guiImages;
AssetManager guiAssets;
constexpr int allowedWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoBringToFrontOnFocus;
constexpr int allowedTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                  ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollY;
struct StyleBinding {
    const char* name;
    ImGuiStyleVar id;
    bool pair;
};
constexpr StyleBinding styleBindings[]{
    {"Alpha", ImGuiStyleVar_Alpha, false},
    {"DisabledAlpha", ImGuiStyleVar_DisabledAlpha, false},
    {"WindowPadding", ImGuiStyleVar_WindowPadding, true},
    {"WindowRounding", ImGuiStyleVar_WindowRounding, false},
    {"WindowBorderSize", ImGuiStyleVar_WindowBorderSize, false},
    {"WindowMinSize", ImGuiStyleVar_WindowMinSize, true},
    {"WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign, true},
    {"ChildRounding", ImGuiStyleVar_ChildRounding, false},
    {"ChildBorderSize", ImGuiStyleVar_ChildBorderSize, false},
    {"PopupRounding", ImGuiStyleVar_PopupRounding, false},
    {"PopupBorderSize", ImGuiStyleVar_PopupBorderSize, false},
    {"FramePadding", ImGuiStyleVar_FramePadding, true},
    {"FrameRounding", ImGuiStyleVar_FrameRounding, false},
    {"FrameBorderSize", ImGuiStyleVar_FrameBorderSize, false},
    {"ItemSpacing", ImGuiStyleVar_ItemSpacing, true},
    {"ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing, true},
    {"IndentSpacing", ImGuiStyleVar_IndentSpacing, false},
    {"CellPadding", ImGuiStyleVar_CellPadding, true},
    {"ScrollbarSize", ImGuiStyleVar_ScrollbarSize, false},
    {"ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding, false},
    {"GrabMinSize", ImGuiStyleVar_GrabMinSize, false},
    {"GrabRounding", ImGuiStyleVar_GrabRounding, false},
    {"TabRounding", ImGuiStyleVar_TabRounding, false},
    {"ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign, true},
    {"SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign, true}};
bool hasWindow() {
    return std::find(scopes.begin(), scopes.end(), 1) != scopes.end();
}
void room(lua_State* L) {
    if (scopes.size() >= 32) {
        luaL_error(L, "GUI scope depth exceeds 32");
    }
}
void pop(lua_State* L, int kind) {
    if (scopes.empty() || scopes.back() != kind) {
        luaL_error(L, "Unbalanced GUI scope end");
    }
    close(kind);
    scopes.pop_back();
}
ImU32 rgba(lua_State* L, int first) {
    float values[4];
    for (int i = 0; i < 4; ++i) {
        values[i] = std::clamp(number(L, first + i, 1), 0.f, 1.f);
    }
    return ImGui::ColorConvertFloat4ToU32({values[0], values[1], values[2], values[3]});
}
ImGuiKey key(lua_State* L) {
    const char* name = text(L, 1, 64);
    for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; ++i) {
        if (!std::strcmp(name, ImGui::GetKeyName(ImGuiKey(i)))) {
            return ImGuiKey(i);
        }
    }
    luaL_error(L, "Unknown GUI key name");
    return ImGuiKey_None;
}
void installGUIConstants(lua_State* L) {
    auto constant = [&](const char* name, int value) {
        lua_pushinteger(L, value);
        lua_setfield(L, -2, name);
    };
    lua_newtable(L);
#define WF(n) constant(#n, ImGuiWindowFlags_##n)
    WF(NoTitleBar);
    WF(NoResize);
    WF(NoMove);
    WF(NoScrollbar);
    WF(NoCollapse);
    WF(AlwaysAutoResize);
    WF(NoBackground);
    WF(MenuBar);
    WF(NoInputs);
    WF(NoNav);
    WF(NoDecoration);
    WF(NoFocusOnAppearing);
    WF(NoBringToFrontOnFocus);
#undef WF
    lua_setfield(L, -2, "WindowFlags");
    lua_newtable(L);
#define TF(n) constant(#n, ImGuiTableFlags_##n)
    TF(Borders);
    TF(RowBg);
    TF(Resizable);
    TF(Reorderable);
    TF(Hideable);
    TF(SizingStretchProp);
    TF(ScrollY);
#undef TF
    lua_setfield(L, -2, "TableFlags");
    lua_newtable(L);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        constant(ImGui::GetStyleColorName(i), i);
    }
    lua_setfield(L, -2, "Col");
    lua_newtable(L);
    for (const auto& style : styleBindings) {
        constant(style.name, style.id);
    }
    lua_setfield(L, -2, "StyleVar");
}
int advancedAPI(lua_State* L, ExtraOp op) {
    // Style stacks, key/mouse queries and background drawing may precede windows.
    if (op == ExtraOp::pushStyleColor) {
        room(L);
        int index = -1;
        if (lua_type(L, 1) == LUA_TSTRING) {
            const char* name = text(L, 1, 64);
            for (int i = 0; i < ImGuiCol_COUNT; ++i) {
                if (!std::strcmp(name, ImGui::GetStyleColorName(i))) {
                    index = i;
                }
            }
        } else {
            const auto value = luaL_checkinteger(L, 1);
            if (value >= 0 && value < ImGuiCol_COUNT) {
                index = int(value);
            }
        }
        if (index < 0) {
            return luaL_error(L, "Unknown GUI style color");
        }
        const auto color = rgba(L, 2);
        ImGui::PushStyleColor(index, color);
        scopes.push_back(11);
        return 0;
    }
    if (op == ExtraOp::pushStyleVar) {
        room(L);
        const StyleBinding* binding = nullptr;
        for (const auto& style : styleBindings) {
            if (lua_type(L, 1) == LUA_TSTRING ? !std::strcmp(text(L, 1, 64), style.name)
                                              : luaL_checkinteger(L, 1) == style.id) {
                binding = &style;
            }
        }
        if (!binding) {
            return luaL_error(L, "Unknown/unsupported GUI style variable");
        }
        const float x = number(L, 2), y = number(L, 3, x);
        if (x < 0 || y < 0 || x > 1000 || y > 1000) {
            return luaL_error(L, "GUI style values must be 0..1000");
        }
        if (binding->pair) {
            ImGui::PushStyleVar(binding->id, ImVec2(x, y));
        } else {
            ImGui::PushStyleVar(binding->id, x);
        }
        scopes.push_back(12);
        return 0;
    }
    if (op == ExtraOp::pushFontSize) {
        room(L);
        const float size = number(L, 1, 16);
        if (size < 8 || size > 96) {
            return luaL_error(L, "Font size must be 8..96");
        }
        ImGui::PushFont(nullptr, size);
        scopes.push_back(13);
        return 0;
    }
    if (op == ExtraOp::popStyleColor || op == ExtraOp::popStyleVar || op == ExtraOp::popFont) {
        const int kind = op == ExtraOp::popStyleColor ? 11 : op == ExtraOp::popStyleVar ? 12 : 13;
        const auto count = luaL_optinteger(L, 1, 1);
        if (count < 1 || count > 32) {
            return luaL_error(L, "GUI pop count must be 1..32");
        }
        if (std::size_t(count) > scopes.size()) {
            return luaL_error(L, "Unbalanced GUI style pop");
        }
        for (int i = 0; i < count; ++i) {
            if (scopes[scopes.size() - 1 - i] != kind) {
                return luaL_error(L, "Unbalanced GUI style pop");
            }
        }
        for (int i = 0; i < count; ++i) {
            pop(L, kind);
        }
        return 0;
    }
    if (op == ExtraOp::getMousePos) {
        const auto mouse = ImGui::GetMousePos();
        lua_pushnumber(L, mouse.x);
        lua_pushnumber(L, mouse.y);
        return 2;
    }
    if (op == ExtraOp::isMouseDown || op == ExtraOp::isMouseClicked ||
        op == ExtraOp::isMouseReleased) {
        const auto button = luaL_checkinteger(L, 1);
        if (button < 0 || button > 4) {
            return luaL_error(L, "Mouse button must be 0..4");
        }
        const bool result = op == ExtraOp::isMouseDown      ? ImGui::IsMouseDown(int(button))
                            : op == ExtraOp::isMouseClicked ? ImGui::IsMouseClicked(int(button))
                                                            : ImGui::IsMouseReleased(int(button));
        lua_pushboolean(L, result);
        return 1;
    }
    if (op == ExtraOp::isKeyDown || op == ExtraOp::isKeyPressed || op == ExtraOp::isKeyReleased) {
        const auto code = key(L);
        const bool repeat = lua_toboolean(L, 2);
        const bool result = op == ExtraOp::isKeyDown      ? ImGui::IsKeyDown(code)
                            : op == ExtraOp::isKeyPressed ? ImGui::IsKeyPressed(code, repeat)
                                                          : ImGui::IsKeyReleased(code);
        lua_pushboolean(L, result);
        return 1;
    }
    if (op >= ExtraOp::drawLine && op <= ExtraOp::drawTriangleFilled) {
        auto* draw = hasWindow() ? ImGui::GetWindowDrawList() : ImGui::GetBackgroundDrawList();
        const float x = number(L, 1), y = number(L, 2);
        if (op == ExtraOp::drawText) {
            const char* value = text(L, 3);
            const auto color = rgba(L, 4);
            draw->AddText({x, y}, color, value);
            return 0;
        }
        const float third = number(L, 3);
        if (op == ExtraOp::drawCircle || op == ExtraOp::drawCircleFilled) {
            const auto color = rgba(L, 4);
            const float width = std::clamp(number(L, 8, 1), .1f, 100.f);
            if (third < 0 || third > 100000) {
                return luaL_error(L, "Circle radius outside 0..100000");
            }
            if (op == ExtraOp::drawCircle) {
                draw->AddCircle({x, y}, third, color, 32, width);
            } else {
                draw->AddCircleFilled({x, y}, third, color, 32);
            }
            return 0;
        }
        const float fourth = number(L, 4);
        if (op == ExtraOp::drawTriangleFilled) {
            const float x3 = number(L, 5), y3 = number(L, 6);
            const auto color = rgba(L, 7);
            draw->AddTriangleFilled({x, y}, {third, fourth}, {x3, y3}, color);
            return 0;
        }
        const auto color = rgba(L, 5);
        const float rounding = std::clamp(number(L, 9), 0.f, 1000.f),
                    width = std::clamp(number(L, 10, 1), .1f, 100.f);
        if (op == ExtraOp::drawLine) {
            draw->AddLine({x, y}, {third, fourth}, color, std::clamp(number(L, 9, 1), .1f, 100.f));
        }
        if (op == ExtraOp::drawRect) {
            draw->AddRect({x, y}, {x + third, y + fourth}, color, rounding, 0, width);
        }
        if (op == ExtraOp::drawRectFilled) {
            draw->AddRectFilled({x, y}, {x + third, y + fourth}, color, rounding);
        }
        return 0;
    }
    if (!hasWindow()) {
        return luaL_error(L, "Call GUI.beginWindow before adding widgets");
    }
    switch (op) {
    case ExtraOp::image:
    case ExtraOp::imageButton: {
        const bool button = op == ExtraOp::imageButton;
        const char* id = button ? text(L, 1, 256) : "";
        const char* path = text(L, button ? 2 : 1);
        const int first = button ? 3 : 2;
        const float width = std::clamp(number(L, first, 64), 1.f, 8192.f),
                    height = std::clamp(number(L, first + 1, 64), 1.f, 8192.f);
        if (!materialTexturePath(path) || !*path) {
            return luaL_error(L, "Expected a project image asset");
        }
        std::string error;
        const auto texture = guiImages.get(guiAssets, path, error, true);
        bool clicked = false;
        if (texture) {
            if (button) {
                clicked =
                    ImGui::ImageButton(id, static_cast<ImTextureID>(texture), {width, height});
            } else {
                ImGui::Image(static_cast<ImTextureID>(texture), {width, height});
            }
        } else {
            ImGui::Dummy({width, height});
        }
        lua_pushboolean(L, button ? clicked : texture != 0);
        return 1;
    }
    case ExtraOp::beginTable: {
        room(L);
        const char* id = text(L, 1, 256);
        const auto columns = luaL_checkinteger(L, 2), flags = luaL_optinteger(L, 3, 0);
        const float height = std::max(0.f, number(L, 4));
        if (columns < 1 || columns > 64 || flags < 0 || flags > 0x7fffffff ||
            (flags & ~allowedTableFlags)) {
            return luaL_error(L, "Invalid table columns or flags");
        }
        if ((flags & ImGuiTableFlags_ScrollY) && height <= 0) {
            return luaL_error(L, "Scrolling table needs a positive height");
        }
        const bool shown = ImGui::BeginTable(id, int(columns), int(flags), {0, height});
        if (shown) {
            scopes.push_back(6);
            tableStates.push_back({int(columns), 0, false});
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endTable:
        pop(L, 6);
        return 0;
    case ExtraOp::tableNextRow:
    case ExtraOp::tableNextColumn:
    case ExtraOp::tableSetColumnIndex:
    case ExtraOp::tableSetupColumn:
    case ExtraOp::tableHeadersRow:
    case ExtraOp::tableGetColumnIndex:
    case ExtraOp::tableGetColumnCount: {
        if (tableStates.empty()) {
            return luaL_error(L, "GUI table operation outside a table");
        }
        auto& table = tableStates.back();
        if (op == ExtraOp::tableSetupColumn) {
            if (table.layout || table.specified >= table.columns) {
                return luaL_error(
                    L, "Table columns must be configured before rows, at most once per column");
            }
            const char* label = text(L, 1, 256);
            const float width = std::max(0.f, number(L, 2));
            ImGui::TableSetupColumn(label,
                                    width > 0 ? ImGuiTableColumnFlags_WidthFixed
                                              : ImGuiTableColumnFlags_WidthStretch,
                                    width);
            ++table.specified;
            return 0;
        }
        if (op == ExtraOp::tableGetColumnCount) {
            lua_pushinteger(L, ImGui::TableGetColumnCount());
            return 1;
        }
        if (op == ExtraOp::tableGetColumnIndex) {
            lua_pushinteger(L, ImGui::TableGetColumnIndex());
            return 1;
        }
        table.layout = true;
        if (op == ExtraOp::tableNextRow) {
            ImGui::TableNextRow();
            return 0;
        }
        if (op == ExtraOp::tableHeadersRow) {
            ImGui::TableHeadersRow();
            return 0;
        }
        if (op == ExtraOp::tableNextColumn) {
            lua_pushboolean(L, ImGui::TableNextColumn());
            return 1;
        }
        const auto index = luaL_checkinteger(L, 1);
        if (index < 0 || index >= table.columns) {
            return luaL_error(L, "Table column index outside range");
        }
        lua_pushboolean(L, ImGui::TableSetColumnIndex(int(index)));
        return 1;
    }
    case ExtraOp::beginMenuBar: {
        room(L);
        const bool shown = ImGui::BeginMenuBar();
        if (shown) {
            scopes.push_back(8);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endMenuBar:
        pop(L, 8);
        return 0;
    case ExtraOp::beginMenu: {
        room(L);
        const char* label = text(L, 1, 256);
        const bool shown = ImGui::BeginMenu(label, lua_isnoneornil(L, 2) || lua_toboolean(L, 2));
        if (shown) {
            scopes.push_back(7);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endMenu:
        pop(L, 7);
        return 0;
    case ExtraOp::menuItem: {
        const char* label = text(L, 1, 256);
        lua_pushboolean(
            L,
            ImGui::MenuItem(
                label, nullptr, lua_toboolean(L, 2), lua_isnoneornil(L, 3) || lua_toboolean(L, 3)));
        return 1;
    }
    case ExtraOp::openPopup:
        ImGui::OpenPopup(text(L, 1, 256));
        return 0;
    case ExtraOp::beginPopup:
    case ExtraOp::beginPopupModal: {
        room(L);
        const char* title = text(L, 1, 256);
        const bool shown = op == ExtraOp::beginPopup
                               ? ImGui::BeginPopup(title)
                               : ImGui::BeginPopupModal(title,
                                                        nullptr,
                                                        ImGuiWindowFlags_AlwaysAutoResize |
                                                            ImGuiWindowFlags_NoSavedSettings);
        if (shown) {
            scopes.push_back(9);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endPopup:
        pop(L, 9);
        return 0;
    case ExtraOp::closeCurrentPopup:
        if (std::find(scopes.begin(), scopes.end(), 9) == scopes.end() &&
            std::find(scopes.begin(), scopes.end(), 7) == scopes.end()) {
            return luaL_error(L, "No current popup");
        }
        ImGui::CloseCurrentPopup();
        return 0;
    case ExtraOp::isPopupOpen:
        lua_pushboolean(L, ImGui::IsPopupOpen(text(L, 1, 256)));
        return 1;
    case ExtraOp::beginCombo: {
        room(L);
        const char* label = text(L, 1, 256);
        const char* preview = text(L, 2, 256);
        const bool shown = ImGui::BeginCombo(label, preview);
        if (shown) {
            scopes.push_back(10);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endCombo:
        pop(L, 10);
        return 0;
    case ExtraOp::combo: {
        const char* label = text(L, 1, 256);
        const auto input = luaL_checkinteger(L, 2);
        luaL_checktype(L, 3, LUA_TTABLE);
        const auto size = lua_rawlen(L, 3);
        if (!size || size > 256 || input < 1 || input > lua_Integer(size)) {
            return luaL_error(L, "Combo needs 1..256 items and a 1-based valid index");
        }
        const char* items[256]{};
        for (std::size_t i = 0; i < size; ++i) {
            lua_geti(L, 3, i + 1);
            items[i] = text(L, -1, 256);
            lua_pop(L, 1);
        }
        int index = int(input) - 1;
        const bool changed = ImGui::Combo(label, &index, items, int(size));
        lua_pushinteger(L, index + 1);
        lua_pushboolean(L, changed);
        return 2;
    }
    case ExtraOp::radioButton: {
        const char* label = text(L, 1, 256);
        lua_pushboolean(L, ImGui::RadioButton(label, lua_toboolean(L, 2)));
        return 1;
    }
    case ExtraOp::beginDragDropSource: {
        room(L);
        const bool shown = ImGui::BeginDragDropSource();
        if (shown) {
            scopes.push_back(14);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endDragDropSource:
        pop(L, 14);
        return 0;
    case ExtraOp::setDragDropPayload: {
        if (scopes.empty() || scopes.back() != 14) {
            return luaL_error(L, "Drag payload requires a source scope");
        }
        const char* type = text(L, 1, 28);
        const char* value = text(L, 2);
        if (!*type) {
            return luaL_error(L, "Empty drag payload type");
        }
        char tag[32];
        std::snprintf(tag, sizeof(tag), "LS:%s", type);
        lua_pushboolean(L, ImGui::SetDragDropPayload(tag, value, std::strlen(value) + 1));
        return 1;
    }
    case ExtraOp::beginDragDropTarget: {
        room(L);
        const bool shown = ImGui::BeginDragDropTarget();
        if (shown) {
            scopes.push_back(15);
        }
        lua_pushboolean(L, shown);
        return 1;
    }
    case ExtraOp::endDragDropTarget:
        pop(L, 15);
        return 0;
    case ExtraOp::acceptDragDropPayload: {
        if (scopes.empty() || scopes.back() != 15) {
            return luaL_error(L, "Accepting payload requires a target scope");
        }
        const char* type = text(L, 1, 28);
        char tag[32];
        std::snprintf(tag, sizeof(tag), "LS:%s", type);
        const auto* payload = ImGui::AcceptDragDropPayload(tag);
        if (!payload || payload->DataSize < 1 || payload->DataSize > 4097 ||
            static_cast<const char*>(payload->Data)[payload->DataSize - 1]) {
            lua_pushnil(L);
        } else {
            lua_pushlstring(L, static_cast<const char*>(payload->Data), payload->DataSize - 1);
        }
        return 1;
    }
    default:
        return luaL_error(L, "Unsupported GUI operation");
    }
}
} // namespace
