#include "editor/search_box.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <imgui.h>

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGuiTextFilter first, second;
    std::strcpy(first.InputBuf, "cat");
    first.Build();
    std::strcpy(second.InputBuf, "dog");
    second.Build();
    ImVec2 clearPoint, textPoint;
    bool changed = false;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(350, 200));
        ImGui::Begin("Search test");
        changed = searchBox("Search assets", first, 250);
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        clearPoint = ImVec2(max.x - (max.y - min.y) * .5f, (min.y + max.y) * .5f);
        textPoint = ImVec2(min.x + 12, (min.y + max.y) * .5f);
        searchBox("Search hierarchy", second, 250);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    assert(first.PassFilter("a cat") && !first.PassFilter("a dog"));
    // The clear button must work even when InputText is actively being edited.
    io.AddMousePosEvent(textPoint.x, textPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddInputCharactersUTF8("s");
    frame();
    assert(changed && std::strchr(first.InputBuf, 's'));
    io.AddMousePosEvent(clearPoint.x, clearPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    assert(changed && !first.IsActive() && first.PassFilter("a dog"));
    assert(second.IsActive() && second.PassFilter("dog") && !second.PassFilter("cat"));
    ImGui::DestroyContext();
    std::puts("Search clear control tests passed");
}
