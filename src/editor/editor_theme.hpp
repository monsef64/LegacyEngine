#pragma once

#include "ui/mygui_ui.hpp"

// The platform registers the embedded Roboto and icon glyph ranges as native
// MyGUI fonts when the context is created. This function applies the editor's
// shared widget palette after a context has been initialized.
inline void applyEditorTheme() {
    ui::styleColorsDark();
    auto& style = ui::style();
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;
    style.WindowPadding = {10, 8};
    style.FramePadding = {6, 4};
    style.ItemSpacing = {8, 6};
    style.ItemInnerSpacing = {6, 4};
    style.IndentSpacing = 16;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;
    style.WindowRounding = 0;
    style.ChildRounding = 4;
    style.FrameRounding = 4;
    style.PopupRounding = 6;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 3;
    style.TabRounding = 4;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.WindowTitleAlign = {0, 0.5f};
    ui::syncThemeToMyGui(false);
}
