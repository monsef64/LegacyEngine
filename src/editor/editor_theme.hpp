#pragma once
#include "editor/editor_palette.hpp"
#include "editor_font.hpp"
#include "editor_icons.hpp"
#include <imgui.h>

// Apply once after context creation. The SDL/OpenGL backends bake fonts at the
// current framebuffer density (including when moving between DPI displays).
inline void applyEditorTheme() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig font;
    font.FontDataOwnedByAtlas = false; // Embedded storage lives for the process.
    font.OversampleH = 3;
    font.OversampleV = 2;
    font.PixelSnapH = false;
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(editorFontData), sizeof(editorFontData), 18.0f, &font);

    static const ImWchar iconRanges[]{0xf000, 0xf8ff, 0};
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.FontDataOwnedByAtlas = false;
    icons.PixelSnapH = true;
    icons.GlyphMinAdvanceX = 20;
    io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(editorIconData), sizeof(editorIconData), 18, &icons, iconRanges);
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.AntiAliasedLines = true;
    s.AntiAliasedLinesUseTex = true;
    s.AntiAliasedFill = true;
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(6, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowRounding = 0; // Keep fixed panels flush with the Scene viewport.
    s.ChildRounding = 4;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 3;
    s.TabRounding = 4;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    auto* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(.88f, .91f, .95f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(.48f, .54f, .63f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(.10f, .12f, .15f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(.09f, .11f, .14f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(.13f, .15f, .19f, 1);
    c[ImGuiCol_Border] = ImVec4(.22f, .26f, .32f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(.16f, .19f, .24f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(.22f, .29f, .38f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(.24f, .36f, .51f, 1);
    c[ImGuiCol_TitleBg] = ImVec4(.08f, .10f, .13f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(.14f, .19f, .26f, 1);
    c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_TitleBg];
    c[ImGuiCol_MenuBarBg] = ImVec4(.08f, .10f, .13f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(.08f, .10f, .13f, 1);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(.27f, .32f, .40f, 1);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(.36f, .43f, .53f, 1);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(.42f, .52f, .65f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(.38f, .70f, 1, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(.30f, .59f, .90f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(.48f, .76f, 1, 1);
    c[ImGuiCol_Button] = ImVec4(.18f, .25f, .34f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(.24f, .41f, .61f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(.20f, .48f, .77f, 1);
    c[ImGuiCol_Header] = ImVec4(.18f, .27f, .38f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(.23f, .38f, .56f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(.24f, .44f, .67f, 1);
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_SeparatorHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_SeparatorActive] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_ResizeGrip] = ImVec4(.30f, .59f, .90f, .25f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(.30f, .59f, .90f, .65f);
    c[ImGuiCol_ResizeGripActive] = c[ImGuiCol_SliderGrab];
    c[ImGuiCol_Tab] = c[ImGuiCol_FrameBg];
    c[ImGuiCol_TabHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_TabSelected] = c[ImGuiCol_Header];
    c[ImGuiCol_TextSelectedBg] = ImVec4(.25f, .53f, .85f, .45f);
    c[ImGuiCol_NavCursor] = c[ImGuiCol_CheckMark];
}
