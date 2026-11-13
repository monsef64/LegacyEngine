#include "editor/editor_theme.hpp"
#include <cassert>
#include <cmath>
#include <initializer_list>

int main() {
    ImGui::CreateContext();
    applyEditorTheme();
    auto& io = ImGui::GetIO();
    assert(io.FontDefault != nullptr);
    assert(io.Fonts->Fonts.Size == 1);
    assert(io.FontDefault->LegacySize == 18.0f);
    // Exercise rasterization at both ordinary and high pixel density.
    for (float density : {1.0f, 2.0f}) {
        io.DisplaySize = ImVec2(1280,720);
        io.DisplayFramebufferScale = ImVec2(density,density);
        io.DeltaTime = 1.0f/60;
        io.Fonts->Build();
        assert(io.FontDefault->IsGlyphInFont(0xf07b) && io.FontDefault->IsGlyphInFont(0xf1c9) && io.FontDefault->IsGlyphInFont(0xf03e));
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10));
        ImGui::SetNextWindowSize(ImVec2(420,160));
        ImGui::Begin("Typography");
        ImGui::TextUnformatted("Hierarchy / Inspector: Position 0.125");
        ImGui::TextUnformatted("\xef\x81\xbb  \xef\x87\x89  \xef\x80\xbe");
        ImGui::End();
        ImGui::Render();
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
    }
    auto& style = ImGui::GetStyle();
    assert(style.AntiAliasedLines && style.AntiAliasedFill);
    assert(style.WindowRounding == 0);
    for (auto& color : style.Colors) {
        assert(std::isfinite(color.x) && color.x >= 0 && color.x <= 1);
        assert(std::isfinite(color.w) && color.w >= 0 && color.w <= 1);
    }
    const auto originalButton=style.Colors[ImGuiCol_Button];const float originalBorder=style.FrameBorderSize;
    applyEditorPalette(2);
    assert(activeEditorTheme==2 && style.FrameBorderSize==1 && style.Colors[ImGuiCol_WindowBg].x>.7f);
    ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({300,200});ImGui::Begin("Classic theme");
    auto* draw=ImGui::GetWindowDrawList();const int start=draw->VtxBuffer.Size;
    draw->AddRectFilled({30,50},{130,80},ImGui::GetColorU32(ImGuiCol_Button));
    ImGui::End();ImGui::Render();finishEditorTheme(ImGui::GetDrawData());
    assert(draw->VtxBuffer[start].col!=draw->VtxBuffer[start+2].col);
    applyEditorPalette(0);
    assert(style.FrameBorderSize==originalBorder && style.Colors[ImGuiCol_Button].x==originalButton.x);
    applyEditorPalette(3);assert(activeEditorTheme==3 && style.Colors[ImGuiCol_WindowBg].x<.2f && style.Colors[ImGuiCol_Header].z>style.Colors[ImGuiCol_Header].x);
    applyEditorPalette(1);assert(style.Colors[ImGuiCol_Button].x==style.Colors[ImGuiCol_Button].y);
    applyEditorPalette(0);
    ImGui::DestroyContext();
}
