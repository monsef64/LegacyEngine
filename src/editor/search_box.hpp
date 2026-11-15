#pragma once
#include <algorithm>
#include <imgui.h>

// Reserve the right-hand end of the field for a clear button, rather than
// overlapping an InputText: the latter can swallow clicks while it has focus.
inline bool searchBox(const char* label, ImGuiTextFilter& filter, float width = -1.0f) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(width);
    const float totalWidth = std::max(2.0f, ImGui::CalcItemWidth());
    const float height = ImGui::GetFrameHeight();
    const float buttonWidth = std::min(height, totalWidth * .5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::SetNextItemWidth(totalWidth - buttonWidth);
    bool changed =
        ImGui::InputTextWithHint("##search", label, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf));
    ImGui::SameLine(0, 0);
    const bool clear = ImGui::InvisibleButton(
        "Clear search", ImVec2(buttonWidth, height), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        min, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
    if (filter.InputBuf[0]) {
        const float centerX = (min.x + max.x) * .5f, centerY = (min.y + max.y) * .5f;
        const float r = std::max(3.0f, buttonWidth * .16f);
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddLine(
            ImVec2(centerX - r, centerY - r), ImVec2(centerX + r, centerY + r), color, 1.5f);
        draw->AddLine(
            ImVec2(centerX - r, centerY + r), ImVec2(centerX + r, centerY - r), color, 1.5f);
        if (hovered) {
            ImGui::SetTooltip("Clear search");
        }
        if (clear) {
            filter.Clear();
            changed = true;
        }
    }
    if (changed) {
        filter.Build();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}
