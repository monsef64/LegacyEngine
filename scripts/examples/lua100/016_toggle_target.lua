-- 016: toggle target
-- Setup: Create Target. Focus Game; press E to toggle Target activity.
-- Attach this script to one object using its LegacyScript component.

local active = true
function onUpdate(dt)
    if isKeyPressed("E") then
        local target = findGameObjectByName("Target")
        if target then active = not active; target:setActive(active) end
    end
end
