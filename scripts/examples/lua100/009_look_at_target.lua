-- 009: look at target
-- Setup: Create an object named Target. Attach to another object.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local target = findGameObjectByName("Target")
    if target then self:lookAt(target) end
end
