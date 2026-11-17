#pragma once

#include "ui/mygui_ui.hpp"

// Theme changes recolour the MyGUI skin atlas in place; embedded fonts and
// widget trees stay intact when the user changes the editor appearance.
inline int activeEditorTheme = 0;

inline void applyEditorPalette(int theme) {
    activeEditorTheme = std::clamp(theme, 0, 3);
    auto& style = ui::style();
    if (activeEditorTheme == 2) {
        ui::styleColorsLight(&style);
        style.FrameRounding = 3;
        style.FrameBorderSize = 1;
    } else {
        ui::styleColorsDark(&style);
        style.FrameRounding = activeEditorTheme == 3 ? 2.0f : 4.0f;
        style.FrameBorderSize = activeEditorTheme == 3 ? 1.0f : 0.0f;
        if (activeEditorTheme == 1) {
            for (auto& color : style.Colors) {
                const float grey = .2126f * color.r + .7152f * color.g + .0722f * color.b;
                color.r = color.g = color.b = grey;
            }
        } else if (activeEditorTheme == 3) {
            style.FrameRounding = 2;
            style.FrameBorderSize = 1;
            style.Colors[ui::Col_WindowBg] = {.18f, .18f, .18f, 1};
            style.Colors[ui::Col_ChildBg] = {.18f, .18f, .18f, 1};
            style.Colors[ui::Col_PopupBg] = {.22f, .22f, .22f, 1};
            style.Colors[ui::Col_Border] = {.10f, .10f, .10f, 1};
            style.Colors[ui::Col_FrameBg] = {.15f, .15f, .15f, 1};
            style.Colors[ui::Col_FrameBgHovered] = {.25f, .25f, .25f, 1};
            style.Colors[ui::Col_FrameBgActive] = {.28f, .28f, .28f, 1};
            style.Colors[ui::Col_TitleBg] = {.16f, .16f, .16f, 1};
            style.Colors[ui::Col_TitleBgActive] = {.22f, .22f, .22f, 1};
            style.Colors[ui::Col_Button] = {.25f, .25f, .25f, 1};
            style.Colors[ui::Col_ButtonHovered] = {.33f, .33f, .33f, 1};
            style.Colors[ui::Col_ButtonActive] = {.24f, .36f, .52f, 1};
            style.Colors[ui::Col_Header] = {.25f, .34f, .47f, 1};
            style.Colors[ui::Col_HeaderHovered] = {.29f, .40f, .55f, 1};
            style.Colors[ui::Col_HeaderActive] = {.29f, .43f, .63f, 1};
            style.Colors[ui::Col_Tab] = {.15f, .15f, .15f, 1};
            style.Colors[ui::Col_TabSelected] = {.26f, .26f, .26f, 1};
            style.Colors[ui::Col_TabHovered] = {.32f, .32f, .32f, 1};
            style.Colors[ui::Col_TabDimmed] = {.15f, .15f, .15f, 1};
            style.Colors[ui::Col_TabDimmedSelected] = {.22f, .22f, .22f, 1};
        }
    }
    ui::syncThemeToMyGui(activeEditorTheme == 2 || activeEditorTheme == 3);
}

inline void finishEditorTheme() {
    // The MyGUI atlas is updated synchronously by applyEditorPalette; no draw
    // packet or font-atlas mutation is necessary after the frame is built.
}
