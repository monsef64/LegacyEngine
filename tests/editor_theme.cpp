#include "editor/editor_theme.hpp"
#include "editor/editor_palette.hpp"
#include <cassert>
#include <cmath>
#include <initializer_list>

int main() {
    ui::createContext();
    applyEditorTheme();
    auto* font = ui::defaultFont();
    assert(font != nullptr);
    ui::renderStandaloneFpsOverlay(1280, 720, "FPS: 60");
    assert(ui::fontSize() >= 18.0f && ui::fontSize() <= 24.0f);
    // Exercise rasterization at both ordinary and high pixel density.
    for (float density : {1.0f, 2.0f}) {
        ui::setDisplaySize(1280, 720, density, density);
        ui::setDeltaTime(1.0f / 60.0f);

        assert(font->hasGlyph(0xf07b) && font->hasGlyph(0xf1c9) && font->hasGlyph(0xf03e));
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(10,10));
        ui::setNextWindowSize(ui::Vec2(420,160));
        ui::beginWindow("Typography");
        ui::textUnformatted("Hierarchy / Inspector: Position 0.125");
        ui::textUnformatted("\xef\x81\xbb  \xef\x87\x89  \xef\x80\xbe");
        ui::endWindow();
        ui::render();
        assert(ui::lastFrameVertexCount() > 0);
    }
    auto& style = ui::style();
    assert(style.AntiAliasedLines && style.AntiAliasedFill);
    assert(style.WindowRounding == 0);
    for (auto& color : style.Colors) {
        assert(std::isfinite(color.x) && color.x >= 0 && color.x <= 1);
        assert(std::isfinite(color.w) && color.w >= 0 && color.w <= 1);
    }
    const auto originalButton=style.Colors[ui::Col_Button];const float originalBorder=style.FrameBorderSize;
    applyEditorPalette(2);
    assert(activeEditorTheme==2 && style.FrameBorderSize==1 && style.Colors[ui::Col_WindowBg].x>.7f);
    const auto classicButton = style.Colors[ui::Col_Button];
    ui::newFrame();ui::setNextWindowPos({10,10});ui::setNextWindowSize({300,200});ui::beginWindow("Classic theme");
    auto* draw=ui::windowDrawList();const auto start=draw->vertices().size();
    draw->addRectFilled({30,50},{130,80},ui::colorU32(ui::Col_Button));
    const auto roundedStart = draw->vertices().size();
    draw->addRect({150,50},{230,100},ui::colorU32(ui::Col_Button),8,2);
    assert(draw->vertices().size() >= roundedStart + 100);
    ui::endWindow();ui::render();finishEditorTheme();
    const auto& vertices = draw->vertices();
    assert(vertices.size() >= start + 6);
    const auto buttonColour = ui::colorU32(ui::Col_Button);
    assert(vertices[start].colour == buttonColour && vertices[start + 2].colour == buttonColour);
    assert(style.FrameBorderSize==1 && style.Colors[ui::Col_Button].x==classicButton.x);
    applyEditorPalette(0);
    assert(style.FrameBorderSize==originalBorder && style.Colors[ui::Col_Button].x==originalButton.x);
    applyEditorPalette(3);assert(activeEditorTheme==3 && style.Colors[ui::Col_WindowBg].x<.2f && style.Colors[ui::Col_Header].z>style.Colors[ui::Col_Header].x);
    applyEditorPalette(1);assert(style.Colors[ui::Col_Button].x==style.Colors[ui::Col_Button].y);
    applyEditorPalette(0);
    ui::destroyContext();
}
