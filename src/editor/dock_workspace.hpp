#pragma once

#include "editor/editor_layout.hpp"

// MyGUI panels are real retained widgets. The workspace owns their split
// regions and native tab buttons; individual editors only open their panel.
inline void drawWorkspace(const EditorRect& toolbar, bool& reset) {
    const auto* viewport = ui::mainViewport();
    const float y = toolbar.y + toolbar.height;
    const float bottom = viewport->Pos.y + viewport->Size.y;
    ui::dockWorkspaceBegin({viewport->Pos.x,
                            y,
                            viewport->Size.x,
                            std::max(1.0f, bottom - y)},
                           reset,
                           panelOpen,
                           7);
}
