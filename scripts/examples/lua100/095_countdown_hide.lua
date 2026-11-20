-- 095: countdown hide
-- Setup: Create a Target object. After five seconds, disables Target once.
-- Attach this script to one object using its LegacyScript component.

local remaining, finished = 5, false
function onUpdate(dt)
    if finished then return end
    remaining = remaining - dt
    if remaining <= 0 then
        finished = true
        local target = findGameObjectByName("Target")
        if target then target:setActive(false) end
    end
end
