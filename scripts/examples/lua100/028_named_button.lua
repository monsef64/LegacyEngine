-- 028: named button
-- Setup: Define an Interact button in Project > Input Settings. Focus Game.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    if isButtonPressed("Interact") then debug.log("Interact action") end
end
