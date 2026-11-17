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

    const float width = std::min(280.f, rect.width - 16), height = 156;
    ImGui::SetNextWindowPos(
        ImVec2(rect.x + rect.width - width - 6, rect.y + rect.height - height - 6));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.065f, .08f, .105f, .97f));
    ImGui::Begin("Particle Effect###SceneParticlePlayback",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(.45f, .78f, 1, 1), "PARTICLE PREVIEW");
    ImGui::Separator();
    bool controlActive = false;
    ImGui::BeginDisabled(runtime);
    if (ImGui::Button(p->paused ? "Resume" : p->playing ? "Pause" : "Play")) {
        if (p->paused) {
            p->resume();
        } else if (p->playing) {
            p->pause();
        } else {
            p->play();
        }
    }
    controlActive |= ImGui::IsItemActive();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        p->stop();
    }
    controlActive |= ImGui::IsItemActive();
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        p->restart();
    }
    controlActive |= ImGui::IsItemActive();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat(
        "Playback speed", &speed, .05f, .05f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    controlActive |= ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::Text("Time: %.2f s", p->time);
    ImGui::Text("Particles: %zu / %d", p->particles.size(), p->maxParticles);
    // Global IsAnyItemActive also includes Transform gizmos and Inspector drags;
    // using it here disabled every transform drag on an emitter.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || controlActive;
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return hovered;
}
