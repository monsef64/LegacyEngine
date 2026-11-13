#pragma once
#include <imgui.h>
#include <imgui_internal.h>
// Theme changes never rebuild the font atlas.
inline int activeEditorTheme = 0;
inline void applyEditorPalette(int theme) {
    activeEditorTheme = theme;
    static float frameRounding = ImGui::GetStyle().FrameRounding,
                 frameBorder = ImGui::GetStyle().FrameBorderSize;
    ImGui::GetStyle().FrameRounding = frameRounding;
    ImGui::GetStyle().FrameBorderSize = frameBorder;
    static ImVec4 original[ImGuiCol_COUNT];
    static bool captured = false;
    if (!captured) {
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            original[i] = ImGui::GetStyle().Colors[i];
        }
        captured = true;
    }
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        auto color = original[i];
        if (theme == 1) {
            float grey = .2126f * color.x + .7152f * color.y + .0722f * color.z;
            color.x = color.y = color.z = grey;
        }
        ImGui::GetStyle().Colors[i] = color;
    }
    if (theme == 2) {
        auto& style = ImGui::GetStyle();
        auto* c = style.Colors;
        ImGui::StyleColorsLight(&style);
        style.FrameRounding = 3;
        style.FrameBorderSize = 1;
        c[ImGuiCol_Text] = {.12f, .12f, .12f, 1};
        c[ImGuiCol_TextDisabled] = {.43f, .43f, .43f, 1};
        c[ImGuiCol_WindowBg] = {.76f, .76f, .76f, 1};
        c[ImGuiCol_ChildBg] = {.79f, .79f, .79f, 1};
        c[ImGuiCol_PopupBg] = {.88f, .88f, .88f, 1};
        c[ImGuiCol_Border] = {.39f, .39f, .39f, 1};
        c[ImGuiCol_FrameBg] = {.94f, .94f, .94f, 1};
        c[ImGuiCol_FrameBgHovered] = {.98f, .98f, .98f, 1};
        c[ImGuiCol_FrameBgActive] = {.82f, .87f, .93f, 1};
        c[ImGuiCol_TitleBg] = {.66f, .66f, .66f, 1};
        c[ImGuiCol_TitleBgActive] = {.70f, .70f, .70f, 1};
        c[ImGuiCol_MenuBarBg] = {.82f, .82f, .82f, 1};
        c[ImGuiCol_Button] = {.74f, .74f, .74f, 1};
        c[ImGuiCol_ButtonHovered] = {.84f, .84f, .84f, 1};
        c[ImGuiCol_ButtonActive] = {.50f, .65f, .81f, 1};
        c[ImGuiCol_Header] = {.40f, .57f, .76f, .65f};
        c[ImGuiCol_HeaderHovered] = {.43f, .63f, .84f, .8f};
        c[ImGuiCol_HeaderActive] = {.30f, .51f, .75f, 1};
        c[ImGuiCol_CheckMark] = {.12f, .29f, .48f, 1};
        c[ImGuiCol_TextSelectedBg] = {.24f, .49f, .78f, .4f};
        c[ImGuiCol_Tab] = {.66f, .66f, .66f, 1};
        c[ImGuiCol_TabSelected] = {.84f, .84f, .84f, 1};
        c[ImGuiCol_TabHovered] = {.78f, .83f, .89f, 1};
        c[ImGuiCol_TabDimmed] = {.64f, .64f, .64f, 1};
        c[ImGuiCol_TabDimmedSelected] = {.76f, .76f, .76f, 1};
    }
    if (theme == 3) {
        auto& style = ImGui::GetStyle();
        auto* c = style.Colors;
        style.FrameRounding = 2;
        style.FrameBorderSize = 1;
        c[ImGuiCol_Text] = {.84f, .84f, .84f, 1};
        c[ImGuiCol_TextDisabled] = {.50f, .50f, .50f, 1};
        c[ImGuiCol_WindowBg] = {.18f, .18f, .18f, 1};
        c[ImGuiCol_ChildBg] = {.18f, .18f, .18f, 1};
        c[ImGuiCol_PopupBg] = {.22f, .22f, .22f, 1};
        c[ImGuiCol_Border] = {.10f, .10f, .10f, 1};
        c[ImGuiCol_FrameBg] = {.15f, .15f, .15f, 1};
        c[ImGuiCol_FrameBgHovered] = {.25f, .25f, .25f, 1};
        c[ImGuiCol_FrameBgActive] = {.28f, .28f, .28f, 1};
        c[ImGuiCol_TitleBg] = {.16f, .16f, .16f, 1};
        c[ImGuiCol_TitleBgActive] = {.22f, .22f, .22f, 1};
        c[ImGuiCol_MenuBarBg] = {.18f, .18f, .18f, 1};
        c[ImGuiCol_Button] = {.25f, .25f, .25f, 1};
        c[ImGuiCol_ButtonHovered] = {.33f, .33f, .33f, 1};
        c[ImGuiCol_ButtonActive] = {.24f, .36f, .52f, 1};
        c[ImGuiCol_Header] = {.25f, .34f, .47f, 1};
        c[ImGuiCol_HeaderHovered] = {.29f, .40f, .55f, 1};
        c[ImGuiCol_HeaderActive] = {.29f, .43f, .63f, 1};
        c[ImGuiCol_CheckMark] = {.78f, .82f, .88f, 1};
        c[ImGuiCol_TextSelectedBg] = {.24f, .39f, .59f, .65f};
        c[ImGuiCol_Tab] = {.15f, .15f, .15f, 1};
        c[ImGuiCol_TabSelected] = {.26f, .26f, .26f, 1};
        c[ImGuiCol_TabHovered] = {.32f, .32f, .32f, 1};
        c[ImGuiCol_TabDimmed] = {.15f, .15f, .15f, 1};
        c[ImGuiCol_TabDimmedSelected] = {.22f, .22f, .22f, 1};
    }
}

// Add a classic top-lit gloss to solid widget fills, without touching text,
// images, custom scene draws, or the font atlas. Run after ImGui::Render().
inline void finishEditorTheme(ImDrawData* data) {
    if ((activeEditorTheme != 2 && activeEditorTheme != 3) || !data) {
        return;
    }
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    const ImU32 colors[]{ImGui::GetColorU32(ImGuiCol_Button),
                         ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                         ImGui::GetColorU32(ImGuiCol_ButtonActive),
                         ImGui::GetColorU32(ImGuiCol_FrameBg)};
    for (int list = 0; list < data->CmdListsCount; ++list) {
        auto& vertices = data->CmdLists[list]->VtxBuffer;
        for (int i = 0; i < vertices.Size;) {
            const auto color = vertices[i].col;
            bool glossy = false;
            for (auto candidate : colors) {
                glossy |= color == candidate;
            }
            if (!glossy || vertices[i].uv.x != white.x || vertices[i].uv.y != white.y) {
                ++i;
                continue;
            }
            int end = i;
            float top = vertices[i].pos.y, bottom = top;
            while (end < vertices.Size && vertices[end].col == color &&
                   vertices[end].uv.x == white.x && vertices[end].uv.y == white.y) {
                top = top < vertices[end].pos.y ? top : vertices[end].pos.y;
                bottom = bottom > vertices[end].pos.y ? bottom : vertices[end].pos.y;
                ++end;
            }
            const auto base = ImGui::ColorConvertU32ToFloat4(color);
            for (int j = i; j < end; ++j) {
                const float t = bottom > top ? (vertices[j].pos.y - top) / (bottom - top) : .5f;
                const float highlight = .15f * (1 - t), shade = .13f * t;
                vertices[j].col = ImGui::ColorConvertFloat4ToU32(
                    {base.x + (1 - base.x) * highlight - base.x * shade,
                     base.y + (1 - base.y) * highlight - base.y * shade,
                     base.z + (1 - base.z) * highlight - base.z * shade,
                     base.w});
            }
            i = end;
        }
    }
}
