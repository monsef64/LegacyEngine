-- Add UI to this object and assign main.rml + hud.rcss (any Assets subfolder).
-- Optional: export a UI reference and drag a different object's UI onto HUD.
-- Attach this script to one object only. Keep the Game cursor unlocked.
expose UI HUD =
local gui
local health, supplies = 100, 0

local function refresh(message)
    gui:setText("health", tostring(health) .. " / 100")
    gui:setText("supplies", tostring(supplies))
    gui:setText("status", message)
end

function onStart()
    gui = HUD or self:getComponent(UI)
    if not gui then
        debug.error("Assign HUD in Inspector, or add UI to this object")
        return
    end
    -- Register once, not every update. Callbacks run through the budgeted
    -- event dispatcher and are removed when the script stops.
    gui:onClick("collect", function()
        supplies = supplies + 1
        refresh("Supply collected. You now have " .. supplies .. ".")
    end)
    gui:onClick("heal", function()
        if supplies > 0 and health < 100 then
            supplies = supplies - 1
            health = math.min(100, health + 10)
            refresh("Used one supply to restore health.")
        elseif health == 100 then
            refresh("Health is already full. Press H to simulate damage.")
        else
            refresh("No supplies left. Collect one first.")
        end
    end)
end

function onUpdate(dt)
    if gui and gui:isLoaded() and isKeyPressed("H") then
        health = math.max(0, health - 10)
        refresh("Took 10 damage.")
    end
end
