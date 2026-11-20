-- 023: key released
-- Setup: Focus Game; press then release E.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    if isKeyReleased("E") then debug.log("E released") end
end
