-- Attach to an active GameObject and press Play. GUI draws each frame in onGUI.
local selected, quality = 1, 0
local message = "Drag an item onto the equipment slot."
local items = {"Sword", "Shield", "Potion"}

function onStart()
    quality = getTextureQuality() + 1
end

function onGUI()
    GUI.setNextWindowSize(460, 380)
    GUI.beginWindow("Inventory", GUI.WindowFlags.MenuBar)
    local showAbout = false
    if GUI.beginMenuBar() then
        if GUI.beginMenu("Options") then
            if GUI.menuItem("About") then showAbout = true end
            GUI.endMenu()
        end
        GUI.endMenuBar()
    end
    if showAbout then GUI.openPopup("About inventory") end
    if GUI.beginPopup("About inventory") then
        GUI.textWrapped("Runtime ImGui: tables, menus, drag/drop and project texture quality.")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
    end
    selected = GUI.combo("Selected", selected, items)
    if GUI.beginTable("Items", 2, GUI.TableFlags.Borders | GUI.TableFlags.RowBg) then
        GUI.tableSetupColumn("Item")
        GUI.tableSetupColumn("Action")
        GUI.tableHeadersRow()
        for i, item in ipairs(items) do
            GUI.pushID(tostring(i))
            GUI.tableNextRow()
            GUI.tableSetColumnIndex(0)
            GUI.text(item)
            GUI.tableNextColumn()
            GUI.button("Drag")
            if GUI.beginDragDropSource() then
                GUI.setDragDropPayload("equipment", item)
                GUI.text(item)
                GUI.endDragDropSource()
            end
            GUI.popID()
        end
        GUI.endTable()
    end
    GUI.button("Equipment slot")
    if GUI.beginDragDropTarget() then
        local item = GUI.acceptDragDropPayload("equipment")
        if item then message = "Equipped " .. item end
        GUI.endDragDropTarget()
    end
    GUI.pushStyleColor(GUI.Col.Text, 1, 0.8, 0.2, 1)
    GUI.textWrapped(message)
    GUI.popStyleColor()
    local changed
    quality, changed = GUI.combo("Texture quality", quality, {"Full", "Half", "Quarter", "Eighth"})
    if changed then setTextureQuality(quality - 1) end
    GUI.endWindow()
end
