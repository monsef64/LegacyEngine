#pragma once
#include "core/components.hpp"
#include "editor/editor_layout.hpp"
#include <imgui.h>
inline bool drawParticlePlayback(
    World& world, Entity selected, const EditorRect& rect, bool runtime, float& speed) {
    auto* p = world.tryGet<ParticleSystem>(selected);
    if (!p || rect.width < 160 || rect.height < 160) {
        return false;
    }

    const float width = std::min(260.f, rect.width - 12), height = 142;
    ImGui::SetNextWindowPos(
        ImVec2(rect.x + rect.width - width - 6, rect.y + rect.height - height - 6));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.08f, .09f, .11f, .94f));
    ImGui::Begin("Particle Effect###SceneParticlePlayback",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted("Particle Effect");
    ImGui::BeginDisabled(runtime);
    if (ImGui::SmallButton(p->paused ? "Resume" : p->playing ? "Pause" : "Play")) {
        if (p->paused) {
            p->resume();
        } else if (p->playing) {
            p->pause();
        } else {
            p->play();
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop")) {
        p->stop();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart")) {
        p->restart();
    }
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat(
        "Playback speed", &speed, .05f, .05f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Text("Time: %.2f s", p->time);
    ImGui::Text("Particles: %zu / %d", p->particles.size(), p->maxParticles);
    const bool hovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemActive();
    ImGui::End();
    ImGui::PopStyleColor();
    return hovered;
}
