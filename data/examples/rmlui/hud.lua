-- Copy main.rml + hud.rcss to Assets/UI/ in your project.
-- Copy this file anywhere in Assets and attach it to one object's LegacyScript.
-- Play, focus Game and unlock the mouse. No Camera or mesh is required for the UI.
-- Use one HUD driver: wasClicked consumes each ID's queued click once.
local health, supplies = 100, 0

local function refresh(message)
    ui.setText("health", tostring(health) .. " / 100")
    ui.setText("supplies", tostring(supplies))
    ui.setText("status", message)
end

function onUpdate(dt)
    -- The document is created on the first runtime frame, not in editor mode.
    if not ui.isLoaded() then return end
    if ui.wasClicked("collect") then
        supplies = supplies + 1
        refresh("Supply collected. You now have " .. supplies .. ".")
    end
    if ui.wasClicked("heal") then
        if supplies > 0 and health < 100 then
            supplies = supplies - 1
            health = math.min(100, health + 10)
            refresh("Used one supply to restore health.")
        elseif health == 100 then
            refresh("Health is already full. Press H to simulate damage.")
        else
            refresh("No supplies left. Collect one first.")
        end
    end
    if isKeyPressed("H") then
        health = math.max(0, health - 10)
        refresh("Took 10 damage.")
    end
end
