#pragma once
#include "core/components.hpp"
#include "editor/editor_layout.hpp"
#include "ui/mygui_ui.hpp"
inline bool drawParticlePlayback(
    World& world, Entity selected, const EditorRect& rect, bool runtime, float& speed) {
    auto* p = world.tryGet<ParticleSystem>(selected);
    if (!p || rect.width < 160 || rect.height < 160) {
        return false;
    }

    const float width = std::min(280.f, rect.width - 16), height = 156;
    ui::setNextWindowPos(
        ui::Vec2(rect.x + rect.width - width - 6, rect.y + rect.height - height - 6));
    ui::setNextWindowSize(ui::Vec2(width, height));
    ui::pushStyleVar(ui::StyleVar_WindowRounding, 8);
    ui::pushStyleVar(ui::StyleVar_WindowPadding, ui::Vec2(12, 10));
    ui::pushStyleColor(ui::Col_WindowBg, ui::Color(.065f, .08f, .105f, .97f));
    ui::beginWindow("Particle Effect###SceneParticlePlayback",
                 nullptr,
                 ui::WindowFlags_NoDecoration | ui::WindowFlags_NoDocking |
                     ui::WindowFlags_NoSavedSettings | ui::WindowFlags_NoMove |
                     ui::WindowFlags_NoScrollbar | ui::WindowFlags_NoScrollWithMouse |
                     ui::WindowFlags_NoFocusOnAppearing);
    ui::textColored(ui::Color(.45f, .78f, 1, 1), "PARTICLE PREVIEW");
    ui::separator();
    bool controlActive = false;
    ui::beginDisabled(runtime);
    if (ui::button(p->paused ? "Resume" : p->playing ? "Pause" : "Play")) {
        if (p->paused) {
            p->resume();
        } else if (p->playing) {
            p->pause();
        } else {
            p->play();
        }
    }
    controlActive |= ui::isItemActive();
    ui::sameLine();
    if (ui::button("Stop")) {
        p->stop();
    }
    controlActive |= ui::isItemActive();
    ui::sameLine();
    if (ui::button("Restart")) {
        p->restart();
    }
    controlActive |= ui::isItemActive();
    ui::setNextItemWidth(90);
    ui::dragFloat(
        "Playback speed", &speed, .05f, .05f, 4.f, "%.2fx", ui::SliderFlags_AlwaysClamp);
    controlActive |= ui::isItemActive();
    ui::endDisabled();
    ui::text("Time: %.2f s", p->time);
    ui::text("Particles: %zu / %d", p->particles.size(), p->maxParticles);
    // Global IsAnyItemActive also includes Transform gizmos and Inspector drags;
    // using it here disabled every transform drag on an emitter.
    const bool hovered = ui::isWindowHovered(ui::HoveredFlags_ChildWindows) || controlActive;
    ui::endWindow();
    ui::popStyleColor();
    ui::popStyleVar(2);
    return hovered;
}
