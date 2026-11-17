#pragma once
#include "ui/mygui_ui.hpp"
enum class PlaybackIcon { Play, Pause, Stop };
inline bool
playbackButton(const char* id, const char* tooltip, PlaybackIcon icon, bool active = false) {
    if (active) {
        ui::pushStyleColor(ui::Col_Button, ui::styleColor(ui::Col_HeaderActive));
    }
    const bool clicked = ui::button(id, ui::Vec2(30, 24));
    if (active) {
        ui::popStyleColor();
    }
    const ui::Vec2 a = ui::itemRectMin(), b = ui::itemRectMax();
    const ui::Vec2 center((a.x + b.x) * .5f, (a.y + b.y) * .5f);
    auto* draw = ui::windowDrawList();
    const auto color = ui::colorU32(ui::Col_Text);
    if (icon == PlaybackIcon::Play) {
        draw->addTriangleFilled(ui::Vec2(center.x - 4, center.y - 6),
                                ui::Vec2(center.x - 4, center.y + 6),
                                ui::Vec2(center.x + 6, center.y),
                                color);
    } else if (icon == PlaybackIcon::Stop) {
        draw->addRectFilled(
            ui::Vec2(center.x - 5, center.y - 5), ui::Vec2(center.x + 5, center.y + 5), color, 1);
    } else {
        draw->addRectFilled(
            ui::Vec2(center.x - 5, center.y - 6), ui::Vec2(center.x - 2, center.y + 6), color, 1);
        draw->addRectFilled(
            ui::Vec2(center.x + 2, center.y - 6), ui::Vec2(center.x + 5, center.y + 6), color, 1);
    }
    if (ui::isItemHovered(ui::HoveredFlags_AllowWhenDisabled)) {
        ui::setTooltip("%s", tooltip);
    }
    return clicked;
}
