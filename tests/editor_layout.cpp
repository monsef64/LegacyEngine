#include "editor/editor_layout.hpp"
#include "editor/dock_workspace.hpp"
#include "scene/editor_grid.hpp"
#include "editor/scene_orientation.hpp"
#include "editor/inspector.hpp"
#include "editor/create_menu.hpp"
#include "editor/script_editor.hpp"
#include "physics/physics.hpp"
#include "ui/mygui_ui.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main(){
    GridSettings grid;require(grid.valid(),"default grid settings valid");grid.cells=0;require(!grid.valid(),"grid size is bounded");grid.cells=512;grid.spacing=.01f;grid.plane=2;require(grid.valid(),"grid supports alternate planes and spacing");grid.color.x=2;require(!grid.valid(),"grid color validated");

    for(ui::Vec2 size:{ui::Vec2(800,600),ui::Vec2(1280,720),ui::Vec2(1920,1080)}) {
        const auto l=EditorLayout::calculate(size.x,size.y,20);
        require(l.hierarchy.x+l.hierarchy.width==l.scene.x,"hierarchy touches scene without overlap");
        require(std::abs(l.scene.x+l.scene.width-l.inspector.x)<0.01f,"scene touches inspector");
        require(std::abs(l.scene.y+l.scene.height-l.lua.y)<0.01f,"scripts below scene");
        require(std::abs(l.console.x+l.console.width-l.inspector.x)<0.01f,"console fits center column");
        require(!l.scene.contains(l.inspector.x+10,l.inspector.y+30),"inspector clicks outside scene");
        require(l.scene.contains(l.scene.x+l.scene.width*0.5f,l.scene.y+l.scene.height*0.5f),"scene center inside viewport");
    }
    for(int density:{1,2}) {
        const auto l=EditorLayout::calculate(1280,720,20);
        const auto pixels=scenePixels(l.scene,1280,720,1280*density,720*density);
        require(pixels.width>0 && pixels.height>0 && pixels.bottom(720*density)>=0,"Scene rectangle maps to framebuffer pixels");
        require(pixels.x==static_cast<int>(l.scene.x*density),"high-DPI viewport origin consistent");
    }
    ui::createContext();auto& io=ui::io();
    io.DisplaySize=ui::Vec2(1280,720);io.DeltaTime=1.0f/60;
    ui::injectKeyMod(ui::Mod_Ctrl,true);
    ui::injectKeyMod(ui::Mod_Shift,true);
    ui::injectKey(ui::Key_S,true);
    ui::newFrame();
    require(!ui::shortcut(ui::Mod_Ctrl | ui::Key_S, ui::InputFlags_RouteGlobal) &&
                ui::shortcut(ui::Mod_Ctrl | ui::Mod_Shift | ui::Key_S,
                             ui::InputFlags_RouteGlobal),
            "shortcut routing requires an exact modifier chord");
    ui::render();
    ui::injectKey(ui::Key_S,false);
    ui::injectKeyMod(ui::Mod_Shift,false);
    ui::injectKeyMod(ui::Mod_Ctrl,false);
    ui::newFrame();
    ui::render();

    World world;
    Entity object=createLevelObject(world,"Selected",{0,1,0},MeshHandle{1});
    const MeshHandle meshes[]{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4}};
    const auto layout=EditorLayout::calculate(1280,720,20);
    auto frame=[&]{ui::newFrame();drawInspector(world,object,layout.inspector,meshes,false);ui::render();};
    frame();frame();
    // Rename through the actual Inspector text field.
    ui::injectMousePos(layout.inspector.x+70,layout.inspector.y+58);frame();
    ui::injectMouseButton(0,true);frame();ui::injectMouseButton(0,false);frame();
    ui::injectKeyMod(ui::Mod_Ctrl,true);ui::injectKey(ui::Key_A,true);frame();
    ui::injectKey(ui::Key_A,false);ui::injectKeyMod(ui::Mod_Ctrl,false);frame();
    ui::injectTextUtf8("Renamed");frame();
    require(world.get<ObjectName>(object).value=="Renamed","Inspector edits live object name");
    ui::injectKey(ui::Key_Enter,true);frame();ui::injectKey(ui::Key_Enter,false);frame();
    // Target the first Transform axis input in the narrow Inspector layout.
    ui::injectMousePos(layout.inspector.x + 70, layout.inspector.y + 262);frame();
    ui::injectKeyMod(ui::Mod_Ctrl,true);frame();
    ui::injectMouseButton(0,true);frame();ui::injectMouseButton(0,false);frame();
    ui::injectKey(ui::Key_A,true);frame();ui::injectKey(ui::Key_A,false);ui::injectKeyMod(ui::Mod_Ctrl,false);frame();
    ui::injectTextUtf8("7");frame();
    ui::injectKey(ui::Key_Enter,true);frame();ui::injectKey(ui::Key_Enter,false);frame();
    require(world.get<Transform>(object).position.x==7,"Inspector edits live Transform position");
    // Removal works from a collapsed component header, with no transport buttons.
    world.add(object,AudioSource{});
    ui::Vec2 headerPosition, removeComponentPosition;
    auto componentFrame=[&]{
        ui::newFrame();ui::setNextWindowPos(ui::Vec2(50,50));ui::setNextWindowSize(ui::Vec2(400,250));
        ui::beginWindow("Component context test");
        if(world.has<AudioSource>(object)){
            headerPosition=ui::cursorScreenPos();ui::setNextItemOpen(false,ui::Cond_Always);
            componentHeader<AudioSource>(world,object,"AudioSource");
            if (ui::isPopupOpen("", ui::PopupFlags_AnyPopupId)) {
                removeComponentPosition = ui::itemRectMin();
            }
        }
        ui::endWindow();ui::render();
    };
    componentFrame();componentFrame();
    ui::injectMousePos(headerPosition.x+80,headerPosition.y+7);componentFrame();
    ui::injectMouseButton(1,true);componentFrame();ui::injectMouseButton(1,false);componentFrame();
    require(ui::isPopupOpen("", ui::PopupFlags_AnyPopupId),
            "right click collapsed header opens component menu");
    const auto popup = ui::topmostPopupRect();
    require(popup.width > 0 && popup.height > 0, "component menu has a visible popup rectangle");
    // Target the final menu row rather than relying on font or popup padding.
    ui::injectMousePos(removeComponentPosition.x + 20, removeComponentPosition.y + 16);componentFrame();
    ui::injectMouseButton(0,true);componentFrame();ui::injectMouseButton(0,false);componentFrame();
    require(!world.has<AudioSource>(object),"context menu removes component");
    bool reset=true;dockedEditor=true;
    auto workspaceFrame = [&] {
        ui::newFrame();drawWorkspace({0,0,1280,40},reset);
        for(const char* name:panelNames){ui::beginWindow(name);ui::textUnformatted("Panel");ui::endWindow();}
        ui::render();
    };
    for(int i=0;i<3;++i) workspaceFrame();
    const auto* hierarchy=ui::findPanel("Hierarchy");
    const auto* inspector=ui::findPanel("Inspector");
    const auto* scene=ui::findPanel("Scene");
    const auto* game=ui::findPanel("Game");
    const auto* assets=ui::findPanel("Assets");
    const auto* script=ui::findPanel("Script Editor");
    const auto* console=ui::findPanel("Console");
    require(hierarchy && inspector && scene && game && assets && script && console,
            "workspace exposes every panel through the adapter");
    require(scene->dockId==game->dockId && assets->dockId==console->dockId,
            "Scene/Game and Assets/Console share tabs");
    require(assets->rect.x<inspector->rect.x && assets->rect.y>hierarchy->rect.y,
            "assets occupies the lower center region");
    require(inspector->rect.x>scene->rect.x && inspector->rect.height>scene->rect.height,
            "Inspector occupies the full right column");
    require(assets->dockTabs==std::vector<std::string>{"Assets", "Script Editor", "Console"},
            "bottom workspace tab order is Assets, Script Editor, Console");
    ui::injectMousePos(520,500);workspaceFrame();
    ui::injectMouseButton(0,true);workspaceFrame();
    ui::injectMouseButton(0,false);workspaceFrame();
    script=ui::findPanel("Script Editor");
    require(script && script->visible && script->activeDockTab=="Script Editor",
            "clicking a dock tab switches the active workspace panel");
    OrbitCamera orientation;
    ui::newFrame();ui::beginWindow("Orientation test");drawSceneOrientation(orientation,{0,0,400,300},true);ui::endWindow();ui::render();

    // Focus offsets address the requested input relative to the next input.
    char focusFirst[64]{}, focusSecond[64]{}, focusThird[64]{}, focusFourth[64]{};
    auto focusOffsetFrame = [&](bool positiveOffset, bool negativeOffset) {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(640, 40), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(420, 300), ui::Cond_Always);
        ui::beginWindow("Keyboard focus offset regression");
        if (positiveOffset) ui::setKeyboardFocusHere(1);
        ui::inputText("Focus first", focusFirst, sizeof(focusFirst));
        ui::inputText("Focus second", focusSecond, sizeof(focusSecond));
        ui::inputText("Focus third", focusThird, sizeof(focusThird));
        if (negativeOffset) ui::setKeyboardFocusHere(-1);
        ui::inputText("Focus fourth", focusFourth, sizeof(focusFourth));
        ui::endWindow();
        ui::render();
    };
    focusOffsetFrame(true, false);
    ui::injectTextUtf8("middle");
    focusOffsetFrame(true, false);
    require(focusFirst[0] == '\0' && std::string(focusSecond) == "middle",
            "positive keyboard focus offset selects the later input");
    focusOffsetFrame(false, true);
    ui::injectTextUtf8("previous");
    focusOffsetFrame(false, true);
    require(std::string(focusThird) == "previous",
            "negative keyboard focus offset selects a previously submitted input");

    // A child with HorizontalScrollbar keeps a persistent scroll offset and
    // applies it to retained widget and screen-space coordinates.
    float childItemX = 0.0f;
    auto horizontalChildFrame = [&] {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(400, 40), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(360, 220), ui::Cond_Always);
        ui::beginWindow("Horizontal child scroll regression");
        if (ui::beginChild("Wide content",
                           ui::Vec2(220, 80),
                           ui::ChildFlags_Borders,
                           ui::WindowFlags_HorizontalScrollbar)) {
            ui::setCursorPosX(380);
            ui::dummy(ui::Vec2(40, 20));
            childItemX = ui::itemRectMin().x;
            ui::endChild();
        }
        ui::endWindow();
        ui::render();
        return childItemX;
    };
    const float childXBefore = horizontalChildFrame();
    ui::injectMousePos(500, 120);
    horizontalChildFrame();
    ui::injectMouseWheel(0, -1);
    horizontalChildFrame();
    const float childXAfter = horizontalChildFrame();
    require(childXBefore - childXAfter >= 25.0f,
            "horizontal wheel scroll moves child content and persists next frame");

    bool clippedChildItemVisible = true;
    ui::newFrame();
    ui::setNextWindowPos(ui::Vec2(40, 80), ui::Cond_Always);
    ui::setNextWindowSize(ui::Vec2(300, 180), ui::Cond_Always);
    ui::beginWindow("Child clipping regression");
    if (ui::beginChild("Short viewport", ui::Vec2(220, 50))) {
        ui::selectable("Visible row", false, ui::SelectableFlags_None, ui::Vec2(0, 20));
        ui::dummy(ui::Vec2(1, 60));
        ui::selectable("Clipped row", false, ui::SelectableFlags_None, ui::Vec2(0, 20));
        clippedChildItemVisible = ui::isItemVisible();
        ui::endChild();
    }
    ui::endWindow();
    ui::render();
    require(!clippedChildItemVisible,
            "item visibility respects child viewports for lazy thumbnail work");

    ui::newFrame();
    ui::setNextWindowPos(ui::Vec2(40, 80), ui::Cond_Always);
    ui::setNextWindowSize(ui::Vec2(300, 140), ui::Cond_Always);
    ui::beginWindow("Indented same-line regression");
    const float availableBeforeIndent = ui::contentRegionAvail().x;
    ui::indent(20);
    const float availableAfterIndent = ui::contentRegionAvail().x;
    const auto firstCursorScreen = ui::cursorScreenPos();
    ui::setCursorScreenPos({firstCursorScreen.x + 12, firstCursorScreen.y});
    const auto movedCursorScreen = ui::cursorScreenPos();
    ui::setCursorScreenPos(firstCursorScreen);
    ui::button("Indented first", ui::Vec2(50, 24));
    const auto firstInlineItem = ui::itemRectMin();
    const auto firstInlineMax = ui::itemRectMax();
    ui::sameLine();
    ui::button("Indented second", ui::Vec2(50, 24));
    const auto secondInlineItem = ui::itemRectMin();
    ui::pushTextWrapPos(ui::cursorPosX() + 96);
    ui::textUnformatted(
        "Tooltip text wraps to the requested width instead of overflowing the preview panel.");
    const float wrappedTextHeight = ui::itemRectMax().y - ui::itemRectMin().y;
    ui::popTextWrapPos();
    ui::pushTextWrapPos();
    ui::textUnformatted(
        "Default wrap position also keeps text inside the current window boundaries.");
    const float defaultWrappedTextHeight = ui::itemRectMax().y - ui::itemRectMin().y;
    ui::popTextWrapPos();
    ui::pushTextWrapPos(ui::cursorPosX() + 48);
    ui::textUnformatted("éééééééééééééééééééééé");
    const float longTokenHeight = ui::itemRectMax().y - ui::itemRectMin().y;
    ui::popTextWrapPos();
    ui::unindent(20);
    ui::endWindow();
    ui::render();
    require(std::abs(availableBeforeIndent - availableAfterIndent - 20.0f) < 1.0f,
            "available content width shrinks with indentation");
    require(std::abs(movedCursorScreen.x - firstCursorScreen.x - 12.0f) < 1.0f &&
                std::abs(firstInlineItem.x - firstCursorScreen.x) < 1.0f,
            "cursor screen conversion includes the current indentation");
    require(std::abs(secondInlineItem.x - firstInlineMax.x - ui::style().ItemSpacing.x) < 1.0f &&
                std::abs(firstInlineItem.y - secondInlineItem.y) < 1.0f,
            "same-line layout preserves indentation without doubling it");
    require(wrappedTextHeight >= ui::textLineHeight() * 2,
            "explicit text wrapping respects the requested local wrap position");
    require(defaultWrappedTextHeight >= ui::textLineHeight() * 2,
            "zero text wrap position uses the current window's right edge");
    require(longTokenHeight >= ui::textLineHeight() * 2,
            "text wrapping can break a long unspaced token at UTF-8 boundaries");

    // Tall table rows use each row's measured height; scroll and freeze flags
    // independently move unfrozen cells while preserving the fixed header/column.
    std::array<std::array<ui::Rect, 2>, 8> tableCells{};
    bool frozenTableCellClicked = false, scrollableTableCellClicked = false;
    bool scrollableOverlayClipped = false;
    ui::Rect frozenHeaderClip, scrollableHeaderClip, frozenBodyClip, scrollableBodyClip;
    ui::DrawClipRect scrollableOverlayClip{};
    float tallestStripedRowBackground = 0.0f;
    auto tableScrollFrame = [&] {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(300, 40), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(420, 280), ui::Cond_Always);
        ui::beginWindow("Table scroll regression");
        const auto backgroundStart = ui::backgroundDrawList()->vertices().size();
        if (ui::beginTable("Frozen table",
                           2,
                           ui::TableFlags_ScrollX | ui::TableFlags_ScrollY |
                               ui::TableFlags_RowBg | ui::TableFlags_BordersInnerV,
                           ui::Vec2(200, 100))) {
            ui::tableSetupColumn("Frozen", ui::TableColumnFlags_WidthFixed, 160);
            ui::tableSetupColumn("Scrollable", ui::TableColumnFlags_WidthFixed, 160);
            ui::tableSetupScrollFreeze(1, 1);
            for (int row = 0; row < static_cast<int>(tableCells.size()); ++row) {
                ui::tableNextRow(24);
                for (int column = 0; column < 2; ++column) {
                    ui::tableNextColumn();
                    ui::pushId(row * 2 + column);
                    char label[48];
                    std::snprintf(label, sizeof(label), "cell %d %d", row, column);
                    const bool clicked = ui::selectable(label,
                                                        false,
                                                        ui::SelectableFlags_None,
                                                        ui::Vec2(0, 40));
                    if (row == 1 && column == 0) frozenTableCellClicked |= clicked;
                    if (row == 1 && column == 1) scrollableTableCellClicked |= clicked;
                    const auto min = ui::itemRectMin();
                    const auto max = ui::itemRectMax();
                    tableCells[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                        {min.x, min.y, max.x - min.x, max.y - min.y};
                    const auto clipMin = ui::windowDrawList()->clipRectMin();
                    const auto clipMax = ui::windowDrawList()->clipRectMax();
                    const ui::Rect cellClip{clipMin.x,
                                            clipMin.y,
                                            clipMax.x - clipMin.x,
                                            clipMax.y - clipMin.y};
                    if (row == 0 && column == 0) frozenHeaderClip = cellClip;
                    if (row == 0 && column == 1) scrollableHeaderClip = cellClip;
                    if (row == 1 && column == 0) frozenBodyClip = cellClip;
                    if (row == 1 && column == 1) {
                        scrollableBodyClip = cellClip;
                        auto* draw = ui::windowDrawList();
                        const auto overlayVertexStart = draw->vertices().size();
                        const float centerX = cellClip.x + cellClip.width * 0.5f;
                        const float centerY = cellClip.y + cellClip.height * 0.5f;
                        draw->addQuadFilled({centerX, centerY - 5},
                                            {centerX + 5, centerY},
                                            {centerX, centerY + 5},
                                            {centerX - 5, centerY},
                                            UI_COL32(255, 197, 70, 255));
                        draw->addLine({cellClip.x - 100, centerY},
                                      {cellClip.x + cellClip.width + 100, centerY},
                                      UI_COL32(235, 90, 75, 255));
                        const auto overlayVertexEnd = draw->vertices().size();
                        for (const auto& command : draw->commands()) {
                            if (command.vertexOffset <= overlayVertexStart &&
                                command.vertexOffset + command.vertexCount >= overlayVertexEnd) {
                                scrollableOverlayClip = command.clip;
                                scrollableOverlayClipped = command.hasClip &&
                                                           std::abs(command.clip.minX - cellClip.x) < 1.0f &&
                                                           std::abs(command.clip.minY - cellClip.y) < 1.0f &&
                                                           std::abs(command.clip.maxX -
                                                                    (cellClip.x + cellClip.width)) < 1.0f &&
                                                           std::abs(command.clip.maxY -
                                                                    (cellClip.y + cellClip.height)) < 1.0f;
                                break;
                            }
                        }
                    }
                    ui::popId();
                }
            }
            ui::endTable();
        }
        const auto& backgroundVertices = ui::backgroundDrawList()->vertices();
        const auto childBackground = ui::colorU32(ui::Col_ChildBg);
        for (std::size_t i = backgroundStart; i + 2 < backgroundVertices.size(); i += 6) {
            if (backgroundVertices[i].colour == childBackground) {
                const float top = (1.0f - backgroundVertices[i].y) * 360.0f;
                const float bottom = (1.0f - backgroundVertices[i + 2].y) * 360.0f;
                tallestStripedRowBackground =
                    std::max(tallestStripedRowBackground, bottom - top);
            }
        }
        ui::endWindow();
        ui::render();
    };
    tableScrollFrame();
    ui::injectMousePos(350, 110);
    tableScrollFrame();
    const float frozenColumnX = tableCells[1][0].x;
    const float scrollingColumnX = tableCells[1][1].x;
    const float frozenRowY = tableCells[0][1].y;
    const float frozenCellCenterX = tableCells[1][0].x + tableCells[1][0].width / 2.0f;
    const float frozenCellCenterY = tableCells[1][0].y + tableCells[1][0].height / 2.0f;
    const float visibleScrollableLeft =
        std::max(tableCells[1][1].x, scrollableBodyClip.x);
    const float visibleScrollableTop =
        std::max(tableCells[1][1].y, scrollableBodyClip.y);
    const float visibleScrollableRight =
        std::min(tableCells[1][1].x + tableCells[1][1].width,
                 scrollableBodyClip.x + scrollableBodyClip.width);
    const float visibleScrollableBottom =
        std::min(tableCells[1][1].y + tableCells[1][1].height,
                 scrollableBodyClip.y + scrollableBodyClip.height);
    const float scrollableCellCenterX =
        (visibleScrollableLeft + visibleScrollableRight) * 0.5f;
    const float scrollableCellCenterY =
        (visibleScrollableTop + visibleScrollableBottom) * 0.5f;
    const float scrollingRowY = tableCells[1][0].y;
    require(std::abs((tableCells[1][0].y - tableCells[0][0].y) - 40.0f) < 1.0f,
            "table rows expand to fit their tallest widget");
    require(tallestStripedRowBackground >= 39.0f,
            "striped table backgrounds expand to cover measured row heights");
    require(std::abs(frozenHeaderClip.width - 160.0f) < 1.0f &&
                std::abs(frozenHeaderClip.height - 40.0f) < 1.0f &&
                std::abs(scrollableHeaderClip.width - 40.0f) < 1.0f &&
                std::abs(scrollableHeaderClip.height - 40.0f) < 1.0f &&
                std::abs(frozenBodyClip.width - 160.0f) < 1.0f &&
                std::abs(frozenBodyClip.height - 60.0f) < 1.0f &&
                std::abs(scrollableBodyClip.width - 40.0f) < 1.0f &&
                std::abs(scrollableBodyClip.height - 60.0f) < 1.0f,
            "all frozen and scrollable table quadrants receive independent clipping regions");
    require(scrollableOverlayClipped &&
                std::abs(scrollableOverlayClip.minX - scrollableBodyClip.x) < 1.0f &&
                std::abs(scrollableOverlayClip.minY - scrollableBodyClip.y) < 1.0f &&
                std::abs(scrollableOverlayClip.maxX -
                         (scrollableBodyClip.x + scrollableBodyClip.width)) < 1.0f &&
                std::abs(scrollableOverlayClip.maxY -
                         (scrollableBodyClip.y + scrollableBodyClip.height)) < 1.0f,
            "dope-sheet key and playhead draw-list overlays inherit the scrollable quadrant clip");
    require(std::abs((tableCells[0][1].x - tableCells[0][0].x) - 160.0f) < 1.0f,
            "table items honor the width of non-first columns");
    ui::injectMousePos(frozenCellCenterX, frozenCellCenterY);
    ui::injectMouseButton(0, true);
    tableScrollFrame();
    ui::injectMouseButton(0, false);
    tableScrollFrame();
    require(frozenTableCellClicked,
            "frozen table cell hit targets remain interactive");
    ui::injectMousePos(scrollableCellCenterX, scrollableCellCenterY);
    ui::injectMouseButton(0, true);
    tableScrollFrame();
    ui::injectMouseButton(0, false);
    tableScrollFrame();
    require(scrollableTableCellClicked,
            "scrollable table cell hit targets remain interactive");
    ui::injectMouseWheel(0, -1);
    tableScrollFrame();
    tableScrollFrame();
    require(std::abs(tableCells[1][0].x - frozenColumnX) < 1.0f &&
                scrollingColumnX - tableCells[1][1].x >= 25.0f,
            "table horizontal scrolling preserves frozen columns");
    ui::injectMouseWheel(-1, 0);
    tableScrollFrame();
    tableScrollFrame();
    require(std::abs(tableCells[0][1].y - frozenRowY) < 1.0f &&
                scrollingRowY - tableCells[1][0].y >= 25.0f,
            "table vertical scrolling preserves frozen rows");

    std::array<ui::Rect, 2> resizeHeaders{}, resizeCells{};
    auto tableResizeFrame = [&] {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(500, 40), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(320, 200), ui::Cond_Always);
        ui::beginWindow("Resizable table regression");
        if (ui::beginTable("Resizable columns", 2, ui::TableFlags_Resizable, ui::Vec2(240, 120))) {
            ui::tableSetupColumn("Left", ui::TableColumnFlags_WidthFixed, 100);
            ui::tableSetupColumn("Right", ui::TableColumnFlags_WidthFixed, 140);
            ui::tableNextRow(28);
            for (int column = 0; column < 2; ++column) {
                ui::tableNextColumn();
                ui::selectable(column == 0 ? "Left header" : "Right header",
                               false,
                               ui::SelectableFlags_None,
                               ui::Vec2(0, 24));
                const auto min = ui::itemRectMin(), max = ui::itemRectMax();
                resizeHeaders[static_cast<std::size_t>(column)] =
                    {min.x, min.y, max.x - min.x, max.y - min.y};
            }
            ui::tableNextRow(28);
            for (int column = 0; column < 2; ++column) {
                ui::tableNextColumn();
                ui::selectable(column == 0 ? "Left data" : "Right data",
                               false,
                               ui::SelectableFlags_None,
                               ui::Vec2(0, 24));
                const auto min = ui::itemRectMin(), max = ui::itemRectMax();
                resizeCells[static_cast<std::size_t>(column)] =
                    {min.x, min.y, max.x - min.x, max.y - min.y};
            }
            ui::endTable();
        }
        ui::endWindow();
        ui::render();
    };
    tableResizeFrame();
    const auto originalResizeWidths = std::array<float, 2>{resizeCells[0].width,
                                                           resizeCells[1].width};
    const float resizeBoundaryX = resizeHeaders[0].x + 100.0f;
    const float resizeHeaderY = resizeHeaders[0].y + resizeHeaders[0].height * 0.5f;
    ui::injectMousePos(resizeBoundaryX, resizeHeaderY);
    tableResizeFrame();
    ui::injectMouseButton(0, true);
    tableResizeFrame();
    ui::injectMousePos(resizeBoundaryX + 24.0f, resizeHeaderY);
    tableResizeFrame();
    ui::injectMouseButton(0, false);
    tableResizeFrame();
    tableResizeFrame();
    require(resizeCells[0].width - originalResizeWidths[0] >= 23.0f &&
                originalResizeWidths[1] - resizeCells[1].width >= 23.0f,
            "resizable table columns persist paired width changes after dragging");

    // The script syntax overlay must use the native EditBox horizontal scroll.
    char longLine[1024]{};
    ui::MultilineEditInfo editInfo;
    auto multilineFrame = [&](bool requestFocus) {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(40, 420), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(260, 180), ui::Cond_Always);
        ui::beginWindow("Horizontal edit scroll regression");
        if (requestFocus) ui::setKeyboardFocusHere();
        ui::inputTextMultiline("Source",
                               longLine,
                               sizeof(longLine),
                               ui::Vec2(180, 90),
                               ui::InputTextFlags_None,
                               nullptr,
                               nullptr,
                               &editInfo);
        ui::endWindow();
        ui::render();
        return editInfo.scrollX;
    };
    multilineFrame(true);
    const std::string wideText(96, 'x');
    ui::injectTextUtf8(wideText.c_str());
    const float editScrollX = multilineFrame(false);
    require(editScrollX > 0.0f,
            "multiline edit reports MyGUI's horizontal offset for syntax highlighting");

    bool dragDropAccepted = false;
    ui::Rect dragSourceRect, dragTargetRect;
    auto dragDropFrame = [&] {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(60, 80), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(340, 160), ui::Cond_Always);
        ui::beginWindow("Drag and drop regression");
        ui::button("Drag source", ui::Vec2(90, 28));
        auto sourceMin = ui::itemRectMin(), sourceMax = ui::itemRectMax();
        dragSourceRect = {sourceMin.x,
                          sourceMin.y,
                          sourceMax.x - sourceMin.x,
                          sourceMax.y - sourceMin.y};
        if (ui::beginDragDropSource()) {
            static constexpr int payloadValue = 42;
            ui::setDragDropPayload("LAYOUT_TEST", &payloadValue, sizeof(payloadValue));
            ui::endDragDropSource();
        }
        ui::sameLine();
        ui::button("Drop target", ui::Vec2(100, 28));
        auto targetMin = ui::itemRectMin(), targetMax = ui::itemRectMax();
        dragTargetRect = {targetMin.x,
                          targetMin.y,
                          targetMax.x - targetMin.x,
                          targetMax.y - targetMin.y};
        if (ui::beginDragDropTarget()) {
            if (const auto* payload = ui::acceptDragDropPayload("LAYOUT_TEST")) {
                dragDropAccepted = payload->size() == sizeof(int);
            }
            ui::endDragDropTarget();
        }
        ui::endWindow();
        ui::render();
    };
    dragDropFrame();
    ui::injectMousePos(dragSourceRect.x + dragSourceRect.width * .5f,
                       dragSourceRect.y + dragSourceRect.height * .5f);
    dragDropFrame();
    ui::injectMouseButton(0, true);
    dragDropFrame();
    ui::injectMousePos(dragSourceRect.x + dragSourceRect.width * .5f + 8,
                       dragSourceRect.y + dragSourceRect.height * .5f);
    dragDropFrame();
    ui::injectMousePos(dragTargetRect.x + dragTargetRect.width * .5f,
                       dragTargetRect.y + dragTargetRect.height * .5f);
    dragDropFrame();
    ui::injectMouseButton(0, false);
    dragDropFrame();
    require(dragDropAccepted, "MyGUI drag payload survives movement from source to target");

    ui::destroyContext();dockedEditor=false;
    // Objects stay alive below the former cleanup cutoff, with their body intact.
    Physics physics;world.get<Transform>(object).position={20,-300,0};
    for(int i=0;i<600;++i)physics.update(world,1.0f/60);
    require(world.alive(object) && world.has<RigidBody>(object),"fallen objects retained as ordinary objects");
    std::puts("Editor panel geometry, Inspector editing and retained fallen-object tests passed");
}
