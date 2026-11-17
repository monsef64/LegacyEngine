#pragma once
#include "core/profiler.hpp"
#include <imgui.h>
inline void drawProfiler(FrameProfiler& profiler, bool& visible) {
    if (!visible) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(690, 530), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Profiler", &visible)) {
        ImGui::Checkbox("Pause sampling", &profiler.paused);
        const auto frame = profiler.stats();
        ImGui::SameLine();
        ImGui::Text("%zu / %zu frames", profiler.count, profiler.capacity);
        ImGui::Text("Frame: %.2f ms | Average: %.2f ms (%.0f FPS) | Peak: %.2f ms",
                    frame.last,
                    frame.average,
                    frame.average > 0 ? 1000 / frame.average : 0,
                    frame.peak);
        float plot[FrameProfiler::capacity]{};
        for (std::size_t i = 0; i < profiler.count; ++i) {
            plot[i] = float(profiler
                                .history[(profiler.next + profiler.capacity - profiler.count + i) %
                                         profiler.capacity]
                                .total);
        }
        ImGui::PlotLines("Frame ms",
                         plot,
                         int(profiler.count),
                         0,
                         nullptr,
                         0,
                         std::max(20.0f, float(frame.peak)),
                         ImVec2(0, 75));
        ImGui::TextWrapped(
            "CPU wall time, including driver calls and vsync wait; not GPU execution time. Render sub-rows are nested within Scene/Game and must not be added to the frame total.");
        if (ImGui::BeginTable("Costs",
                              5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp)) {
            for (const char* title : {"Subsystem", "Last ms", "Avg ms", "Peak ms", "Avg frame %"}) {
                ImGui::TableSetupColumn(title);
            }
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < FrameProfiler::parts; ++i) {
                auto stats = profiler.stats(int(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(profileNames[i]);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", stats.last);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", stats.average);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", stats.peak);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", frame.average > 0 ? 100 * stats.average / frame.average : 0);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
