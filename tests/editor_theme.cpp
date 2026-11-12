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
    assert(io.FontDefault->LegacySize == 16.0f);
    // Exercise rasterization at both ordinary and high pixel density.
    for (float density : {1.0f, 2.0f}) {
        io.DisplaySize = ImVec2(1280,720);
        io.DisplayFramebufferScale = ImVec2(density,density);
        io.DeltaTime = 1.0f/60;
        io.Fonts->Build();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10));
        ImGui::SetNextWindowSize(ImVec2(420,160));
        ImGui::Begin("Typography");
        ImGui::TextUnformatted("Hierarchy / Inspector: Position 0.125");
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
    ImGui::DestroyContext();
}
