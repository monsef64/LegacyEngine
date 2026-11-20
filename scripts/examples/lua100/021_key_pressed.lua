-- 021: key pressed
-- Setup: Focus Game. Prints once per Space press.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    if isKeyPressed("Space") then debug.log("Space pressed") end
end
