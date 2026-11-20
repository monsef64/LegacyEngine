-- 022: key held
-- Setup: Focus Game. Demonstrates held state without log spam.
-- Attach this script to one object using its LegacyScript component.

local held = 0
function onUpdate(dt)
    if isKeyDown("W") then held = held + dt end
    if isKeyReleased("W") then debug.log("Held seconds:", held); held = 0 end
end
